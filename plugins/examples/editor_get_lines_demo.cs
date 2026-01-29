fn on_load() {
  fm.notify("editor_get_lines demo loaded");
  fm.bind("^L", "show_editor_lines");
}

fn show_editor_lines(key) {
  if (!fm.editor_active()) {
    fm.notify("Editor not active");
    return true;
  }

  let lines = fm.editor_get_lines(1, 5);
  if (lines == nil) {
    fm.notify("No lines returned");
    return true;
  }

  fm.console("--- editor lines 1-5 ---");
  let i = 0;
  let count = len(lines);
  while (i < count) {
    fm.console(fmt("%d: %s", i + 1, lines[i]));
    i = i + 1;
  }
  return true;
}
