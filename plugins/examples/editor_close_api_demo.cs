// editor_close_api_demo.cs
// Example plugin demonstrating fm.editor_close()
//
// In the editor, press Ctrl+Q to close the editor via the API.

fn on_load() {
  fm.console("editor_close_api_demo loaded");
  fm.console("In the editor: press ^Q to close via fm.editor_close()");
  fm.bind("^Q", "close_editor");
}

fn close_editor(key) {
  if (!fm.editor_active()) {
    fm.notify("Editor not active");
    return true;
  }

  let ok = fm.editor_close();
  if (!ok) {
    fm.notify("Close failed");
    return true;
  }

  fm.notify("Closing editor...");
  return true;
}
