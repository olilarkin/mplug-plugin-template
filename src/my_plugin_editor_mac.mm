#include "my_plugin.h"

#if defined(__APPLE__)

#import <Cocoa/Cocoa.h>

#include <choc/gui/choc_WebView.h>
#include <choc/gui/choc_MessageLoop.h>
#include <choc/containers/choc_Value.h>

#include <array>
#include <memory>
#include <sstream>
#include <string>

namespace
{

// Self-contained editor UI. HTML/CSS/JS are embedded as a string — the
// cross-platform-safe approach (no resource files to bundle into each format).
//
// Parameter sync is bidirectional:
//   * UI -> host: a drag is bracketed by beginGesture/endGesture (so the host
//     records clean automation) with setParameter for each value change.
//   * host -> UI: onParameterChange() updates the control WITHOUT firing
//     setParameter — programmatically assigning slider.value emits no 'input'
//     event, so host-originated updates can't bounce back as UI edits.
std::string generateEditorHTML()
{
  return R"HTML(<!DOCTYPE html>
<html>
<head>
  <meta charset="utf-8">
  <style>
    * { box-sizing: border-box; margin: 0; padding: 0; }
    body {
      font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', sans-serif;
      background: #1e1e2e; color: #cdd6f4;
      height: 100vh; display: flex; flex-direction: column;
      align-items: center; justify-content: center; gap: 20px; user-select: none;
    }
    h1 { font-size: 16px; font-weight: 600; letter-spacing: 0.5px; }
    .readout { font-size: 40px; font-weight: 700; color: #89b4fa; font-variant-numeric: tabular-nums; }
    input[type="range"] {
      -webkit-appearance: none; width: 280px; height: 6px;
      background: #313244; border-radius: 3px; outline: none;
    }
    input[type="range"]::-webkit-slider-thumb {
      -webkit-appearance: none; width: 20px; height: 20px;
      background: #89b4fa; border-radius: 50%; cursor: pointer;
    }
  </style>
</head>
<body>
  <h1>GAIN</h1>
  <div class="readout" id="readout">0.0 dB</div>
  <input type="range" id="gain" min="-60" max="12" step="0.1" value="0">
  <script>
    const PARAM = 0;
    const slider = document.getElementById('gain');
    const readout = document.getElementById('readout');

    let gestureActive = false;
    let idleTimer = null;

    // Coalesce value sends to one per animation frame (latest value only). The
    // JS->C++ bridge does a round-trip per call; an uncoalesced drag floods it
    // and the host sees changes late. We render locally every frame regardless.
    let pendingValue = null;
    let rafId = null;

    function flushPending() {
      rafId = null;
      if (pendingValue !== null) {
        setParameter(PARAM, pendingValue);   // -> C++: value change
        pendingValue = null;
      }
    }

    function render(db) {
      readout.textContent = db.toFixed(1) + ' dB';
    }

    // host -> UI. Update the control only; never call setParameter (assigning
    // slider.value does NOT fire 'input', so there's no feedback loop). Ignore
    // updates mid-drag so we don't fight the user's pointer.
    window.onParameterChange = function(index, value) {
      if (index !== PARAM || gestureActive) return;
      slider.value = value;
      render(value);
    };

    function beginIfNeeded() {
      if (!gestureActive) {
        gestureActive = true;
        beginGesture(PARAM);            // -> C++: host records gesture start
      }
    }

    function endIfNeeded() {
      if (idleTimer !== null) { clearTimeout(idleTimer); idleTimer = null; }
      if (rafId !== null) { cancelAnimationFrame(rafId); rafId = null; }
      flushPending();                   // make sure the host gets the final value
      if (gestureActive) {
        gestureActive = false;
        endGesture(PARAM);              // -> C++: host records gesture end
      }
    }

    // Lazy-begin on first 'input' covers keyboard/wheel (no pointerdown);
    // pointerdown covers the common drag case.
    slider.addEventListener('pointerdown', beginIfNeeded);

    slider.addEventListener('input', () => {
      const db = parseFloat(slider.value);
      beginIfNeeded();
      render(db);                       // instant local readout
      pendingValue = db;                // coalesce: send latest once per frame
      if (rafId === null) rafId = requestAnimationFrame(flushPending);

      // Safety net: end the gesture if a pointerup/change never arrives
      // (e.g. focus loss), so the host doesn't get stuck in write mode.
      if (idleTimer !== null) clearTimeout(idleTimer);
      idleTimer = setTimeout(endIfNeeded, 250);
    });

    slider.addEventListener('pointerup', endIfNeeded);
    slider.addEventListener('change', endIfNeeded);

    // Pull the current value from the plugin on load.
    getParameter(PARAM).then((db) => {
      slider.value = db;
      render(db);
    });
  </script>
</body>
</html>)HTML";
}

}  // namespace

// Per-instance editor storage.
struct MyPluginEditor
{
  std::unique_ptr<choc::ui::WebView> webView;

  // True while the UI is actively editing a parameter — host->UI pushes for that
  // index are suppressed so they don't fight the user's drag. Accessed only on
  // the UI/message thread (JS bindings + poll timer), so a plain bool is fine.
  std::array<bool, MyPlugin::parameterCount()> editing{};

  // Drains host-originated parameter changes to the WebView. Declared last so it
  // is destroyed first (stopping the callback before the WebView goes away).
  choc::messageloop::Timer pollTimer;
};

namespace
{
// host -> UI: push a single parameter value into the WebView.
void pushParameterToJS(MyPluginEditor& editor, std::size_t index, double value)
{
  std::ostringstream js;
  js << "if (window.onParameterChange) window.onParameterChange(" << index << ", " << value << ");";
  editor.webView->evaluateJavascript(js.str());
}
}  // namespace

void* MyPlugin::createEditor(void* parentView, mplug::WindowType windowType)
{
  if (windowType != mplug::WindowType::Cocoa)
    return nullptr;

  @autoreleasepool
  {
    NSView* parent = (__bridge NSView*)parentView;

    auto* editor = new MyPluginEditor();

    choc::ui::WebView::Options opts;
    opts.enableDebugMode = false;
    editor->webView = std::make_unique<choc::ui::WebView>(opts);

    // JS -> C++: apply a parameter value. Route through the EditorHost so the
    // host's parameter view + automation track the edit. Fall back to a direct
    // write when no host is available (e.g. AU/CLAP until they adopt EditorHost).
    editor->webView->bind("setParameter", [this](const choc::value::ValueView& args) -> choc::value::Value
    {
      if (args.isArray() && args.size() >= 2)
      {
        const auto index = static_cast<std::size_t>(args[0].getInt64());
        const auto value = args[1].getFloat64();
        if (mEditorHost)
          mEditorHost->performParameterEdit(index, value);
        else
          setParameterValue(index, value);
      }
      return {};
    });

    // JS -> C++: gesture start (begin automation touch/latch).
    editor->webView->bind("beginGesture", [this, editor](const choc::value::ValueView& args) -> choc::value::Value
    {
      if (args.isArray() && args.size() >= 1)
      {
        const auto index = static_cast<std::size_t>(args[0].getInt64());
        if (index < editor->editing.size())
          editor->editing[index] = true;
        if (mEditorHost)
          mEditorHost->beginParameterGesture(index);
      }
      return {};
    });

    // JS -> C++: gesture end.
    editor->webView->bind("endGesture", [this, editor](const choc::value::ValueView& args) -> choc::value::Value
    {
      if (args.isArray() && args.size() >= 1)
      {
        const auto index = static_cast<std::size_t>(args[0].getInt64());
        if (mEditorHost)
          mEditorHost->endParameterGesture(index);
        if (index < editor->editing.size())
          editor->editing[index] = false;
      }
      return {};
    });

    // JS -> C++: read a parameter value (returned as a JS Promise).
    editor->webView->bind("getParameter", [this](const choc::value::ValueView& args) -> choc::value::Value
    {
      std::size_t index = (args.isArray() && args.size() >= 1) ? static_cast<std::size_t>(args[0].getInt64()) : 0;
      return choc::value::createFloat64(getParameterValue(index));
    });

    editor->webView->setHTML(generateEditorHTML());

    // host -> UI: drain parameter changes (automation, generic UI, preset
    // recall) on the message thread and push them to the WebView.
    editor->pollTimer = choc::messageloop::Timer(30, [this, editor]() -> bool
    {
      if (mEditorHost)
      {
        // A bulk state change (preset / setState) asks for a full re-read.
        if (mEditorHost->consumeFullRefresh())
        {
          for (std::size_t i = 0; i < MyPlugin::parameterCount(); ++i)
            pushParameterToJS(*editor, i, getParameterValue(i));
        }

        mplug::ParameterChange change;
        while (mEditorHost->popParameterChange(change))
        {
          const bool busy = change.index < editor->editing.size() && editor->editing[change.index];
          if (!busy)
            pushParameterToJS(*editor, change.index, change.value);
        }
      }
      return true;  // keep running
    });

    void* webViewHandle = editor->webView->getViewHandle();
    NSView* webViewNSView = (__bridge NSView*)webViewHandle;
    auto size = defaultEditorSize();
    webViewNSView.frame = NSMakeRect(0, 0, size.width, size.height);
    webViewNSView.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;

    if (parent)
      [parent addSubview:webViewNSView];

    mEditorView = editor;
    return webViewHandle;
  }
}

void MyPlugin::destroyEditor()
{
  if (!mEditorView)
    return;

  @autoreleasepool
  {
    auto* editor = static_cast<MyPluginEditor*>(mEditorView);

    // Stop the poll timer before tearing down the WebView so its callback can't
    // fire against a half-destroyed view.
    editor->pollTimer.clear();

    if (editor->webView)
    {
      NSView* webViewNSView = (__bridge NSView*)editor->webView->getViewHandle();
      [webViewNSView removeFromSuperview];
    }
    delete editor;
    mEditorView = nullptr;
  }
}

#endif  // __APPLE__
