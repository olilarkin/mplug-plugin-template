#include "my_plugin.h"

#if defined(__APPLE__)

#import <Cocoa/Cocoa.h>

#include <choc/gui/choc_WebView.h>
#include <choc/containers/choc_Value.h>

#include <memory>
#include <string>

namespace
{

// Self-contained editor UI. HTML/CSS/JS are embedded as a string — the
// cross-platform-safe approach (no resource files to bundle into each format).
std::string generateEditorHTML(double gainDb)
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
    const slider = document.getElementById('gain');
    const readout = document.getElementById('readout');

    function render(db) {
      readout.textContent = db.toFixed(1) + ' dB';
    }

    slider.addEventListener('input', () => {
      const db = parseFloat(slider.value);
      render(db);
      setParameter(0, db);          // -> C++ binding
    });

    // Pull the current value from the plugin on load.
    getParameter(0).then((db) => {
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
};

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

    // JS -> C++: set a parameter value.
    editor->webView->bind("setParameter", [this](const choc::value::ValueView& args) -> choc::value::Value
    {
      if (args.isArray() && args.size() >= 2)
        setParameterValue(static_cast<std::size_t>(args[0].getInt64()), args[1].getFloat64());
      return {};
    });

    // JS -> C++: read a parameter value (returned as a JS Promise).
    editor->webView->bind("getParameter", [this](const choc::value::ValueView& args) -> choc::value::Value
    {
      std::size_t index = (args.isArray() && args.size() >= 1) ? static_cast<std::size_t>(args[0].getInt64()) : 0;
      return choc::value::createFloat64(getParameterValue(index));
    });

    editor->webView->setHTML(generateEditorHTML(getParameterValue(0)));

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
