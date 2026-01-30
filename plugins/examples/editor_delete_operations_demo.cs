// editor_delete_operations_demo.cs - Demonstrates fm.editor_delete_range API
//
// This plugin provides various text deletion operations using the
// fm.editor_delete_range API function.

fn on_load() {
  fm.notify("editor_delete_operations_demo loaded");
  fm.bind("^D", "delete_selection");
  fm.bind("^K", "delete_to_end_of_line");
  fm.bind("^W", "delete_word_backward");
  fm.bind("M-d", "delete_word_forward");
}

// Delete the current selection
fn delete_selection(key) {
  if (!fm.editor_active()) {
    fm.notify("Editor not active");
    return true;
  }

  let sel = fm.editor_get_selection();
  if (sel == nil) {
    fm.notify("No text selected");
    return true;
  }

  let start_line = sel["start_line"];
  let start_col = sel["start_col"];
  let end_line = sel["end_line"];
  let end_col = sel["end_col"];
  
  if (fm.editor_delete_range(start_line, start_col, end_line, end_col)) {
    fm.notify("Selection deleted");
  } else {
    fm.notify("Failed to delete selection");
  }
  
  return true;
}

// Delete from cursor to end of line (like Emacs Ctrl+K)
fn delete_to_end_of_line(key) {
  if (!fm.editor_active()) {
    fm.notify("Editor not active");
    return true;
  }

  let cursor = fm.editor_get_cursor();
  if (cursor == nil) {
    fm.notify("Failed to get cursor");
    return true;
  }

  let line_num = cursor["line"];
  let col = cursor["col"];
  
  let line = fm.editor_get_line(line_num);
  if (line == nil) {
    fm.notify("Failed to get line");
    return true;
  }
  
  let line_len = len(line);
  
  if (col > line_len) {
    fm.notify("Cursor past end of line");
    return true;
  }
  
  if (col == line_len + 1) {
    // At end of line - delete newline if not last line
    let line_count = fm.editor_line_count();
    if (line_num < line_count) {
      // Join with next line
      if (fm.editor_delete_range(line_num, col, line_num + 1, 1)) {
        fm.notify("Line joined");
      } else {
        fm.notify("Failed to join line");
      }
    } else {
      fm.notify("At end of file");
    }
  } else {
    // Delete from cursor to end of line
    if (fm.editor_delete_range(line_num, col, line_num, line_len + 1)) {
      fm.notify("Deleted to end of line");
    } else {
      fm.notify("Failed to delete");
    }
  }
  
  return true;
}

// Delete word backward from cursor (like Emacs Ctrl+W)
fn delete_word_backward(key) {
  if (!fm.editor_active()) {
    fm.notify("Editor not active");
    return true;
  }

  let cursor = fm.editor_get_cursor();
  if (cursor == nil) {
    return true;
  }

  let line_num = cursor["line"];
  let col = cursor["col"];
  
  let line = fm.editor_get_line(line_num);
  if (line == nil) {
    return true;
  }
  
  // Find start of previous word
  let word_start = find_word_start_backward(line, col - 1);
  
  if (word_start >= 0) {
    if (fm.editor_delete_range(line_num, word_start + 1, line_num, col)) {
      fm.notify("Word deleted");
    }
  } else {
    fm.notify("No word to delete");
  }
  
  return true;
}

// Delete word forward from cursor (like Emacs Meta+D)
fn delete_word_forward(key) {
  if (!fm.editor_active()) {
    fm.notify("Editor not active");
    return true;
  }

  let cursor = fm.editor_get_cursor();
  if (cursor == nil) {
    return true;
  }

  let line_num = cursor["line"];
  let col = cursor["col"];
  
  let line = fm.editor_get_line(line_num);
  if (line == nil) {
    return true;
  }
  
  // Find end of next word
  let word_end = find_word_end_forward(line, col - 1);
  
  if (word_end > col - 1) {
    if (fm.editor_delete_range(line_num, col, line_num, word_end + 1)) {
      fm.notify("Word deleted");
    }
  } else {
    fm.notify("No word to delete");
  }
  
  return true;
}

// Helper: Find start of word backward from position (0-indexed)
fn find_word_start_backward(line, pos) {
  if (pos <= 0) {
    return -1;
  }
  
  let len_val = len(line);
  if (pos > len_val) {
    pos = len_val;
  }
  
  // Skip trailing whitespace
  while (pos > 0 && is_whitespace(substr(line, pos - 1, 1))) {
    pos = pos - 1;
  }
  
  if (pos == 0) {
    return 0;
  }
  
  // Find word start
  while (pos > 0 && !is_whitespace(substr(line, pos - 1, 1))) {
    pos = pos - 1;
  }
  
  return pos;
}

// Helper: Find end of word forward from position (0-indexed)
fn find_word_end_forward(line, pos) {
  let len_val = len(line);
  
  if (pos >= len_val) {
    return len_val;
  }
  
  // Skip leading whitespace
  while (pos < len_val && is_whitespace(substr(line, pos, 1))) {
    pos = pos + 1;
  }
  
  // Find word end
  while (pos < len_val && !is_whitespace(substr(line, pos, 1))) {
    pos = pos + 1;
  }
  
  return pos;
}

// Helper: Check if character is whitespace
fn is_whitespace(ch) {
  return ch == " " || ch == "\t" || ch == "\n" || ch == "\r";
}
