// editor_save_api_demo.cs
// Example plugin demonstrating fm.editor_save()
//
// This plugin intercepts Ctrl+S while the editor is open and saves via the API.

fn on_load() {
  fm.console("editor_save_api_demo loaded");
  fm.console("In the editor: press ^S to save via fm.editor_save()");
  fm.bind("^S", "save_current_file");
}

fn save_current_file(key) {
  if (!fm.editor_active()) {
    fm.notify("Editor not active");
    return true;
  }

  let ok = fm.editor_save();
  if (!ok) {
    fm.notify("Save failed");
    return true;
  }

  let path = fm.editor_get_path();
  if (path != nil) {
    fm.notify("Saved: " + path);
  } else {
    fm.notify("Saved");
  }
  return true;
}
