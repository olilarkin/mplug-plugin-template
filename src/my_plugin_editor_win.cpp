#include "my_plugin.h"

#if defined(_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <choc/gui/choc_WebView.h>
#include <choc/containers/choc_Value.h>

#include "my_plugin_editor_html.h"

#include <memory>

// Per-instance editor storage.
struct MyPluginEditor
{
  std::unique_ptr<choc::ui::WebView> webView;
};

void* MyPlugin::createEditor(void* parentView, mplug::WindowType windowType)
{
  if (windowType != mplug::WindowType::Win32)
    return nullptr;

  HWND parent = static_cast<HWND>(parentView);

  auto editor = std::make_unique<MyPluginEditor>();

  choc::ui::WebView::Options opts;
  opts.enableDebugMode = false;
  editor->webView = std::make_unique<choc::ui::WebView>(opts);

  // choc returns null if the WebView2 runtime is unavailable (it ships on
  // Windows 11 and most Windows 10 machines). Fail gracefully so the host falls
  // back to its generic parameter UI rather than showing an empty window.
  HWND webViewHwnd = static_cast<HWND>(editor->webView->getViewHandle());
  if (!webViewHwnd)
    return nullptr;

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

  editor->webView->setHTML(myplugin::generateEditorHTML(getParameterValue(0)));

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
