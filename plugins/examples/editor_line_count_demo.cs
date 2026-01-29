fn on_load() {
  fm.notify("editor_line_count demo loaded");
  fm.bind("^K", "show_line_count");
}

fn show_line_count(key) {
  if (!fm.editor_active()) {
    fm.notify("Editor not active");
    return true;
  }

  let count = fm.editor_line_count();
  fm.notify(fmt("Editor has %d lines", count));
  fm.console(fmt("=== Editor Statistics ==="));
  fm.console(fmt("Total lines: %d", count));
  fm.console(fmt("File: %s", fm.editor_get_path()));
  return true;
}
