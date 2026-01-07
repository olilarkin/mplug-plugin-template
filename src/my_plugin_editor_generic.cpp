#include "my_plugin.h"

#if !defined(__APPLE__) && !defined(_WIN32)

// The in-host WebView editor is currently implemented for macOS only. On Linux
// (and other platforms) the host shows its generic parameter UI, and the
// standalone App provides a full cross-platform WebView UI. Returning nullptr
// here keeps all plugin formats building and loading cleanly.
//
// To add a native in-host editor, create a choc::ui::WebView, bind the
// parameter functions (see src/my_plugin_editor_mac.mm), and re-parent its
// GtkWidget (choc::ui::WebView::getViewHandle()) under the host-provided
// parentView.

void* MyPlugin::createEditor(void*, mplug::WindowType)
{
  return nullptr;
}

void MyPlugin::destroyEditor()
{
  mEditorView = nullptr;
}

#endif  // !__APPLE__ && !_WIN32
