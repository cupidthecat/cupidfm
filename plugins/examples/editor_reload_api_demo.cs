// editor_reload_api_demo.cs
// Example plugin demonstrating fm.editor_reload()
//
// In the editor, press Ctrl+R to reload the current file from disk.

fn on_load() {
  fm.console("editor_reload_api_demo loaded");
  fm.console("In the editor: press ^R to reload via fm.editor_reload()");
  fm.bind("^R", "reload_editor_file");
}

fn reload_editor_file(key) {
  if (!fm.editor_active()) {
    fm.notify("Editor not active");
    return true;
  }

  let ok = fm.editor_reload();
  if (!ok) {
    fm.notify("Reload failed");
    return true;
  }

  fm.notify("Reloading from disk...");
  return true;
}

