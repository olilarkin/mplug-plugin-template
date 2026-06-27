#include "my_plugin.h"

#if defined(__APPLE__)

#import <Cocoa/Cocoa.h>

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

    editor->webView->setHTML(myplugin::generateEditorHTML(getParameterValue(0)));

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
