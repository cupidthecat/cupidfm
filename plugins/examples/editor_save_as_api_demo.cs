// editor_save_as_api_demo.cs
// Example plugin demonstrating fm.editor_save_as(path)
//
// In the editor, press Ctrl+Shift+S to save to a new path.

fn on_load() {
  fm.console("editor_save_as_api_demo loaded");
  fm.console("In the editor: press ^D to Save As via fm.editor_save_as(path)");
  fm.bind("^D", "save_as");
}

fn save_as(key) {
  if (!fm.editor_active()) {
    fm.notify("Editor not active");
    return true;
  }

  let current = fm.editor_get_path();
  let initial = current != nil ? current : "";

  let path = fm.prompt("Save As (path):", initial);
  if (path == nil || path == "") {
    return true;
  }

  let ok = fm.editor_save_as(path);
  if (!ok) {
    fm.notify("Save As failed");
    return true;
  }

  fm.notify("Saved As: " + path);
  fm.reload();
  fm.select(path_basename(path));
  return true;
}
