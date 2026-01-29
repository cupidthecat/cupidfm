fn on_load() {
  fm.notify("editor_selection demo loaded");
  fm.bind("^S", "show_selection_info");
}

fn show_selection_info(key) {
  if (!fm.editor_active()) {
    fm.notify("Editor not active");
    return true;
  }

  let selection = fm.editor_get_selection();
  if (selection == nil) {
    fm.notify("No text selected");
    return true;
  }

  let start_line = selection["start_line"];
  let start_col = selection["start_col"];
  let end_line = selection["end_line"];
  let end_col = selection["end_col"];
  
  // Calculate selection size
  let line_count = end_line - start_line + 1;
  
  fm.notify(fmt("Selection: %d lines", line_count));
  fm.console(fmt("=== Selection Info ==="));
  fm.console(fmt("Start: line %d, col %d", start_line, start_col));
  fm.console(fmt("End: line %d, col %d", end_line, end_col));
  fm.console(fmt("Lines selected: %d", line_count));
  
  // Get the selected text (if it's not too large)
  if (line_count <= 10) {
    fm.console(fmt("--- Selected Text ---"));
    let i = start_line;
    while (i <= end_line) {
      let line = fm.editor_get_line(i);
      if (line != nil) {
        fm.console(line);
      }
      i = i + 1;
    }
  }
  
  return true;
}
