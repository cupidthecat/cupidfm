fn on_load() {
  fm.notify("editor_text_manipulation demo loaded");
  fm.bind("^T", "insert_timestamp");
  fm.bind("^U", "uppercase_selection");
}

fn insert_timestamp(key) {
  if (!fm.editor_active()) {
    fm.notify("Editor not active");
    return true;
  }

  // Get current timestamp
  let timestamp = fmt("[%s] ", "2026-01-29 12:34:56");  // In real use, get actual time
  
  if (fm.editor_insert_text(timestamp)) {
    fm.notify("Timestamp inserted");
  } else {
    fm.notify("Failed to insert timestamp");
  }
  
  return true;
}

fn uppercase_selection(key) {
  if (!fm.editor_active()) {
    fm.notify("Editor not active");
    return true;
  }

  let sel = fm.editor_get_selection();
  if (sel == nil) {
    fm.notify("No text selected");
    return true;
  }
  
  // Use the built-in C function for efficient uppercase conversion
  if (fm.editor_uppercase_selection()) {
    fm.notify("Text converted to uppercase");
  } else {
    fm.notify("Failed to convert text");
  }
  
  return true;
}
