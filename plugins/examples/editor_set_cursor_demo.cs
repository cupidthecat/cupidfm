// editor_set_cursor_demo.cs - Demonstrates fm.editor_set_cursor() API
//
// Keybindings:
//   Ctrl+G - Jump to specific line/column (prompts for input)
//   Ctrl+T - Jump to top of file (line 1, col 1)
//   Ctrl+B - Jump to bottom of file (last line, col 1)
//   Alt+M - Jump to middle of file
//   Alt+N - Jump to next occurrence of selected text

fn on_load() {
  fm.notify("editor_set_cursor demo loaded");
  fm.bind("^G", "goto_line_col");
  fm.bind("^T", "goto_top");
  fm.bind("^B", "goto_bottom");
  fm.bind("^N", "goto_middle");
  fm.bind("M-n", "goto_next_match");
}

// Jump to a specific line and column (prompts user)
fn goto_line_col(key) {
  if (!fm.editor_active()) {
    fm.notify("Editor not active");
    return true;
  }

  let line_str = fm.prompt("Go to line:", "1");
  if (line_str == nil || line_str == "") {
    return true;
  }

  let col_str = fm.prompt("Go to column:", "1");
  if (col_str == nil || col_str == "") {
    return true;
  }

  let line = to_int(line_str);
  let col = to_int(col_str);
  
  if (fm.editor_set_cursor(line, col)) {
    fm.notify(fmt("Jumped to line %d, column %d", line, col));
  } else {
    fm.notify(fmt("Failed to jump to line %d, column %d (invalid position)", line, col));
  }

  return true;
}

// Jump to the top of the file (line 1, column 1)
fn goto_top(key) {
  if (!fm.editor_active()) {
    fm.notify("Editor not active");
    return true;
  }

  if (fm.editor_set_cursor(1, 1)) {
    fm.notify("Jumped to top of file");
  } else {
    fm.notify("Failed to jump to top");
  }

  return true;
}

// Jump to the bottom of the file (last line, column 1)
fn goto_bottom(key) {
  if (!fm.editor_active()) {
    fm.notify("Editor not active");
    return true;
  }

  let total_lines = fm.editor_line_count();
  if (total_lines == 0) {
    fm.notify("File is empty");
    return true;
  }

  if (fm.editor_set_cursor(total_lines, 1)) {
    fm.notify(fmt("Jumped to bottom (line %d)", total_lines));
  } else {
    fm.notify("Failed to jump to bottom");
  }

  return true;
}

// Jump to the middle of the file
fn goto_middle(key) {
  if (!fm.editor_active()) {
    fm.notify("Editor not active");
    return true;
  }

  let total_lines = fm.editor_line_count();
  if (total_lines == 0) {
    fm.notify("File is empty");
    return true;
  }

  // Get cursor info to get proper integer types
  let cursor = fm.editor_get_cursor();
  if (cursor == nil) {
    fm.notify("Failed to get cursor");
    return true;
  }

  // Calculate middle using the same type as line numbers from cursor
  let half = total_lines / 2;
  let middle_line = cursor.line;  // Start with same type as cursor.line
  
  // Simple loop to set middle_line to half of total (avoiding to_int issues)
  let count = 0;
  while (count < half) {
    count = count + 1;
  }
  middle_line = count;
  
  // Use column from cursor to ensure type consistency
  let target_col = cursor.line;  // Get an integer
  target_col = 1;  // Then set to 1
  
  if (fm.editor_set_cursor(middle_line, target_col)) {
    fm.notify(fmt("Jumped to middle (line %d of %d)", middle_line, total_lines));
  } else {
    fm.notify("Failed to jump to middle");
  }

  return true;
}

// Jump to the next occurrence of selected text
fn goto_next_match(key) {
  if (!fm.editor_active()) {
    fm.notify("Editor not active");
    return true;
  }

  let sel = fm.editor_get_selection();
  if (sel == nil) {
    fm.notify("No text selected");
    return true;
  }

  // Get the selected text
  let start_line = sel["start_line"];
  let start_col = sel["start_col"];
  let end_line = sel["end_line"];
  let end_col = sel["end_col"];

  // For simplicity, only handle single-line selections
  if (start_line != end_line) {
    fm.notify("Multi-line selections not supported for search");
    return true;
  }

  let line_text = fm.editor_get_line(start_line);
  if (line_text == nil) {
    fm.notify("Failed to get line text");
    return true;
  }

  // Extract selected text (CupidScript strings are 1-indexed like the editor API)
  let search_text = substr(line_text, start_col, end_col - start_col);
  if (search_text == "" || search_text == nil) {
    fm.notify("Empty selection");
    return true;
  }

  // Search for next occurrence starting from current position
  let total_lines = fm.editor_line_count();
  let found = false;
  let found_line = 0;
  let found_col = 0;

  // Start searching from the line after the selection
  for i in range(start_line + 1, total_lines + 1) {
    let line = fm.editor_get_line(i);
    if (line == nil) {
      continue;
    }

    // Simple substring search (case-sensitive)
    let pos = find(line, search_text);
    if (pos >= 0) {
      found = true;
      found_line = i;
      found_col = pos + 1;  // Convert to 1-indexed
      break;
    }
  }

  // Wrap around to beginning if not found
  if (!found) {
    for i in range(1, start_line + 1) {
      let line = fm.editor_get_line(i);
      if (line == nil) {
        continue;
      }

      let pos = find(line, search_text);
      if (pos >= 0) {
        found = true;
        found_line = i;
        found_col = pos + 1;
        break;
      }
    }
  }

  if (found) {
    if (fm.editor_set_cursor(found_line, found_col)) {
      fm.notify(fmt("Found '%s' at line %d, col %d", search_text, found_line, found_col));
    } else {
      fm.notify("Failed to jump to match");
    }
  } else {
    fm.notify(fmt("'%s' not found", search_text));
  }

  return true;
}
