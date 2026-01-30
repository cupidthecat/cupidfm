// editor_find_replace_demo.cs - Demonstrates fm.editor_replace_text API
//
// This plugin provides find-and-replace functionality in the editor
// using the fm.editor_replace_text API function.

fn on_load() {
  fm.notify("editor_find_replace_demo loaded");
  fm.bind("^F", "find_replace_in_selection");
  fm.bind("^R", "replace_all_in_file");
}

// Find and replace text within the current selection
fn find_replace_in_selection(key) {
  if (!fm.editor_active()) {
    fm.notify("Editor not active");
    return true;
  }

  let sel = fm.editor_get_selection();
  if (sel == nil) {
    fm.notify("No text selected");
    return true;
  }

  let find = fm.prompt("Find:", "");
  if (find == nil || find == "") {
    return true;
  }

  let replace = fm.prompt("Replace with:", "");
  if (replace == nil) {
    return true;
  }

  let start_line = sel["start_line"];
  let start_col  = sel["start_col"];
  let end_line   = sel["end_line"];
  let end_col    = sel["end_col"];

  // Get selected text
  let text = get_text_in_range(start_line, start_col, end_line, end_col);
  if (text == nil) {
    fm.notify("Failed to get text");
    return true;
  }

  // Count occurrences
  let count = count_occurrences(text, find);
  if (count == 0) {
    fm.notify(fmt("'%s' not found in selection", find));
    return true;
  }

  // Perform replacement
  let new_text = str_replace(text, find, replace);

  // Replace the text in the editor
  if (fm.editor_replace_text(start_line, start_col, end_line, end_col, new_text)) {
    fm.notify(fmt("Replaced %d occurrence(s)", count));
  } else {
    fm.notify("Failed to replace text");
  }

  return true;
}

// Replace all occurrences in the entire file
fn replace_all_in_file(key) {
  if (!fm.editor_active()) {
    fm.notify("Editor not active");
    return true;
  }

  let find = fm.prompt("Find:", "");
  if (find == nil || find == "") {
    return true;
  }

  let replace = fm.prompt("Replace with:", "");
  if (replace == nil) {
    return true;
  }

  let line_count = fm.editor_line_count();
  if (line_count == 0) {
    fm.notify("Empty file");
    return true;
  }

  // Get all lines
  let lines = fm.editor_get_lines(1, line_count);
  if (lines == nil) {
    fm.notify("Failed to get file content");
    return true;
  }

  // Join all lines into one text block
  let text = "";
  let i = 0;
  while (i < len(lines)) {
    text = text + lines[i];
    if (i < len(lines) - 1) {
      text = text + "\n";
    }
    i = i + 1;
  }

  // Count occurrences
  let count = count_occurrences(text, find);
  if (count == 0) {
    fm.notify(fmt("'%s' not found in file", find));
    return true;
  }

  // Ask for confirmation
  let msg = fmt("Replace %d occurrence(s)?", count);
  if (!fm.confirm("Confirm Replace", msg)) {
    return true;
  }

  // Perform replacement
  let new_text = str_replace(text, find, replace);

  // Get the last line's length (use +1 for exclusive end-col)
  let last_line = lines[len(lines) - 1];
  let last_col = len(last_line) + 1;

  // Replace entire file content (line 1 col 1 to last line, last col)
  if (fm.editor_replace_text(1, 1, line_count, last_col, new_text)) {
    fm.notify(fmt("Replaced %d occurrence(s)", count));
  } else {
    fm.notify("Failed to replace text");
  }

  return true;
}

// Helper: Get text in a range
fn get_text_in_range(start_line, start_col, end_line, end_col) {
  let text = "";
  let i = start_line;

  while (i <= end_line) {
    let line = fm.editor_get_line(i);
    if (line == nil) {
      return nil;
    }

    if (i == start_line && i == end_line) {
      // Single line selection
      let line_len = len(line);
      // allow end_col == line_len + 1 (exclusive end)
      if (start_col <= line_len + 1 && end_col <= line_len + 1 && end_col >= start_col) {
        text = text + substr(line, start_col - 1, end_col - start_col);
      }
    } else if (i == start_line) {
      // First line
      text = text + substr(line, start_col - 1, len(line) - start_col + 1) + "\n";
    } else if (i == end_line) {
      // Last line (end_col is exclusive)
      text = text + substr(line, 0, end_col - 1);
    } else {
      // Middle lines
      text = text + line + "\n";
    }

    i = i + 1;
  }

  return text;
}

// Helper: Simple string replace (replaces all occurrences)
fn str_replace(text, find, replace) {
  let result = "";
  let pos = 0;
  let find_len = len(find);
  let text_len = len(text);

  while (pos < text_len) {
    // Check if we have a match at current position
    let is_match = true;   // renamed from `match`
    let i = 0;

    while (i < find_len && pos + i < text_len) {
      if (substr(text, pos + i, 1) != substr(find, i, 1)) {
        is_match = false;
        break;
      }
      i = i + 1;
    }

    if (is_match && i == find_len) {
      // Found a match, append replacement
      result = result + replace;
      pos = pos + find_len;
    } else {
      // No match, append current character
      result = result + substr(text, pos, 1);
      pos = pos + 1;
    }
  }

  return result;
}

// Helper: Count occurrences of find string in text
fn count_occurrences(text, find) {
  let count = 0;
  let pos = 0;
  let find_len = len(find);
  let text_len = len(text);

  while (pos < text_len) {
    let is_match = true;   // renamed from `match`
    let i = 0;

    while (i < find_len && pos + i < text_len) {
      if (substr(text, pos + i, 1) != substr(find, i, 1)) {
        is_match = false;
        break;
      }
      i = i + 1;
    }

    if (is_match && i == find_len) {
      count = count + 1;
      pos = pos + find_len;
    } else {
      pos = pos + 1;
    }
  }

  return count;
}
