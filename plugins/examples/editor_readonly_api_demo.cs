// editor_readonly_api_demo.cs
// Example plugin demonstrating fm.editor_set_readonly(readonly)
//
// In the editor, press Ctrl+Shift+O to toggle read-only mode.

let is_ro = false;

fn on_load() {
  fm.console("editor_readonly_api_demo loaded");
  fm.console("In the editor: press ^D to toggle read-only");
  fm.bind("^D", "toggle_ro");
}

fn toggle_ro(key) {
  if (!fm.editor_active()) {
    fm.notify("Editor not active");
    return true;
  }

  is_ro = !is_ro;
  let ok = fm.editor_set_readonly(is_ro);
  if (!ok) {
    fm.notify("Failed to set read-only");
    return true;
  }

  fm.notify(is_ro ? "Editor: READ-ONLY" : "Editor: writable");
  return true;
}

