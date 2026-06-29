#include "my_plugin.h"

#if defined(_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX  // keep windows.h min/max macros from clobbering choc's std::max()
#endif
#include <windows.h>

#include <choc/gui/choc_WebView.h>
#include <choc/gui/choc_MessageLoop.h>
#include <choc/containers/choc_Value.h>

// Generated from resources/web/editor.html at configure time (see CMakeLists.txt).
// Provides std::string myplugin::editorHTML() — the same GUI as the macOS editor.
#include "my_plugin_editor_html.h"

#include <array>
#include <locale>
#include <memory>
#include <sstream>
#include <string>

// Per-instance editor storage. Mirrors the macOS editor (my_plugin_editor_mac.mm):
// the bidirectional parameter sync, gesture handling and host->UI poll timer are
// identical — only the native windowing (WebView2 / HWND) differs.
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
  // Format with the classic ("C") locale so a host that switched the global C++
  // locale to one with a comma decimal separator can't corrupt the value — e.g.
  // -12.3 becoming "-12,3", which JS reads as two arguments (wrong value).
  std::ostringstream js;
  js.imbue(std::locale::classic());
  js << "if (window.onParameterChange) window.onParameterChange(" << index << ", " << value << ");";
  editor.webView->evaluateJavascript(js.str());
}
}  // namespace

void* MyPlugin::createEditor(void* parentView, mplug::WindowType windowType)
{
  if (windowType != mplug::WindowType::Win32)
    return nullptr;

  HWND parent = static_cast<HWND>(parentView);

  auto editor = std::make_unique<MyPluginEditor>();

  choc::ui::WebView::Options opts;
  opts.enableDebugMode = false;
  editor->webView = std::make_unique<choc::ui::WebView>(opts);

  // choc returns a null handle if the WebView2 runtime is unavailable (it ships
  // on Windows 11 and most Windows 10 machines). Fail gracefully so the host
  // falls back to its generic parameter UI rather than showing an empty window.
  HWND webViewHwnd = static_cast<HWND>(editor->webView->getViewHandle());
  if (!webViewHwnd)
    return nullptr;

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
  editor->webView->bind("beginGesture", [this, e = editor.get()](const choc::value::ValueView& args) -> choc::value::Value
  {
    if (args.isArray() && args.size() >= 1)
    {
      const auto index = static_cast<std::size_t>(args[0].getInt64());
      if (index < e->editing.size())
        e->editing[index] = true;
      if (mEditorHost)
        mEditorHost->beginParameterGesture(index);
    }
    return {};
  });

  // JS -> C++: gesture end.
  editor->webView->bind("endGesture", [this, e = editor.get()](const choc::value::ValueView& args) -> choc::value::Value
  {
    if (args.isArray() && args.size() >= 1)
    {
      const auto index = static_cast<std::size_t>(args[0].getInt64());
      if (mEditorHost)
        mEditorHost->endParameterGesture(index);
      if (index < e->editing.size())
        e->editing[index] = false;
    }
    return {};
  });

  // JS -> C++: read a parameter value (returned as a JS Promise).
  editor->webView->bind("getParameter", [this](const choc::value::ValueView& args) -> choc::value::Value
  {
    std::size_t index = (args.isArray() && args.size() >= 1) ? static_cast<std::size_t>(args[0].getInt64()) : 0;
    return choc::value::createFloat64(getParameterValue(index));
  });

  editor->webView->setHTML(myplugin::editorHTML());

  // host -> UI: drain parameter changes (automation, generic UI, preset recall)
  // on the message thread and push them to the WebView.
  editor->pollTimer = choc::messageloop::Timer(30, [this, e = editor.get()]() -> bool
  {
    if (mEditorHost)
    {
      // A bulk state change (preset / setState) asks for a full re-read.
      if (mEditorHost->consumeFullRefresh())
      {
        for (std::size_t i = 0; i < MyPlugin::parameterCount(); ++i)
          pushParameterToJS(*e, i, getParameterValue(i));
      }

      mplug::ParameterChange change;
      while (mEditorHost->popParameterChange(change))
      {
        const bool busy = change.index < e->editing.size() && e->editing[change.index];
        if (!busy)
          pushParameterToJS(*e, change.index, change.value);
      }
    }
    return true;  // keep running
  });

  // choc creates the WebView2 host window as a top-level WS_POPUP. Convert it to
  // a child of the host-provided parent and size it to the editor. choc's own
  // window proc handles WM_SIZE and re-fits the WebView2 controller to the new
  // client area, so resizing the host window resizes the web content too.
  auto size = defaultEditorSize();
  SetParent(webViewHwnd, parent);
  SetWindowLongPtrW(webViewHwnd, GWL_STYLE, WS_CHILD | WS_VISIBLE);
  SetWindowPos(webViewHwnd, nullptr, 0, 0, size.width, size.height,
               SWP_NOZORDER | SWP_FRAMECHANGED | SWP_SHOWWINDOW);

  mEditorView = editor.release();
  return webViewHwnd;
}

void MyPlugin::destroyEditor()
{
  if (!mEditorView)
    return;

  auto* editor = static_cast<MyPluginEditor*>(mEditorView);

  // Stop the poll timer before tearing down the WebView so its callback can't
  // fire against a half-destroyed view.
  editor->pollTimer.clear();

  if (editor->webView)
  {
    // Detach from the host window before the WebView (and its HWND) is torn down.
    if (HWND webViewHwnd = static_cast<HWND>(editor->webView->getViewHandle()))
      SetParent(webViewHwnd, nullptr);
  }
  delete editor;
  mEditorView = nullptr;
}

#endif  // _WIN32
