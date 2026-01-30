// editor_change_demo.cs
// Example plugin demonstrating on_editor_change() callback
// This callback fires whenever editor content is modified

let change_count = 0;
let last_change_time = 0;
let total_chars_added = 0;

fn on_load() {
    fm.console("Editor change tracker loaded");
    fm.console("Note: on_editor_change currently requires full editor integration");
    fm.console("Press ^D to see change statistics");
}

fn on_key(key) {
    // Ctrl+S: Show change statistics
    if (key == "^D") {
        if (!fm.editor_active()) {
            fm.popup("Change Stats", "No file is currently open in the editor");
            return true;
        }
        
        let stats = fmt("Editor Change Statistics:\n\n");
        stats = stats + fmt("Total changes: %d\n", change_count);
        stats = stats + fmt("Characters added: %d\n", total_chars_added);
        
        fm.popup("Change Statistics", stats);
        return true;
    }
    
    return false;
}

// This callback will fire whenever the editor content changes
// Parameters:
//   line: The line number where the change occurred (1-indexed)
//   col: The column number where the change occurred (1-indexed)
//   text: The text that was inserted/deleted
fn on_editor_change(line, col, text) {
    change_count = change_count + 1;
    
    if (text != nil) {
        total_chars_added = total_chars_added + len(text);
    }
    
    // Log significant changes to console
    if (len(text) > 10) {
        fm.console(fmt("Large change at line %d: %d chars", line, len(text)));
    }
    
    // Example: Auto-save after 100 changes
    if (change_count % 100 == 0) {
        fm.notify(fmt("Auto-check point: %d changes made", change_count));
    }
}

fn on_editor_open(path) {
    // Reset statistics for new file
    change_count = 0;
    total_chars_added = 0;
    fm.console(fmt("Tracking changes for: %s", path));
}
