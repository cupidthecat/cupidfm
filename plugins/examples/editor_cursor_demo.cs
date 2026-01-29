fn on_load() {
  fm.notify("editor_cursor demo loaded");
  fm.bind("^P", "show_cursor_position");
}

fn show_cursor_position(key) {
  if (!fm.editor_active()) {
    fm.notify("Editor not active");
    return true;
  }

  let cursor = fm.editor_get_cursor();
  if (cursor == nil) {
    fm.notify("Failed to get cursor position");
    return true;
  }

  let line = cursor["line"];
  let col = cursor["col"];
  let total_lines = fm.editor_line_count();
  
  fm.notify(fmt("Cursor at line %d, col %d", line, col));
  fm.console(fmt("=== Cursor Position ==="));
  fm.console(fmt("Line: %d of %d", line, total_lines));
  fm.console(fmt("Column: %d", col));
  fm.console(fmt("File: %s", fm.editor_get_path()));
  
  return true;
}
