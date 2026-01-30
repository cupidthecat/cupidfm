// editor_save_demo.cs
// Example plugin demonstrating on_editor_save() callback
// This callback fires whenever a file is saved in the editor

let save_count = 0;
let last_saved_file = "";
let save_history = [];
let total_saves_this_session = 0;

fn on_load() {
    fm.console("Editor Save Demo loaded!");
    fm.console("Press Ctrl+S in the editor to save files");
    fm.console("Press Ctrl+D to view save statistics");
}

fn on_key(key) {
    // Press Ctrl+D to display save statistics
    if (key == "^D") {
        if (!fm.editor_active()) {
            fm.notify("Editor save demo: Open a file first");
            return true;
        }
        
        // Display comprehensive save statistics
        let stats = "=== Save Statistics ===\n";
        stats = stats + fmt("Total saves this session: %d\n", total_saves_this_session);
        stats = stats + "Last saved file: " + last_saved_file + "\n";
        stats = stats + fmt("Recent save history (%d files):\n", len(save_history));
        
        for (let i = 0; i < len(save_history) && i < 10; i = i + 1) {
            let entry = save_history[i];
            stats = stats + fmt("  %d. %s\n", i + 1, entry);
        }
        
        fm.popup("Save Statistics", stats);
        return true;
    }
    
    return false;
}

// This callback will fire whenever a file is saved
// Parameter:
//   path: The absolute path of the file that was saved
fn on_editor_save(path) {
    save_count = save_count + 1;
    total_saves_this_session = total_saves_this_session + 1;
    last_saved_file = path;
    
    // Extract filename from path
    let filename = path_basename(path);
    
    // Add to history (keep last 20)
    save_history = [path, ...save_history];
    if (len(save_history) > 20) {
        save_history = slice(save_history, 0, 20);
    }
    
    // Show notification
    fm.console(fmt("File saved: %s (save #%d)", filename, total_saves_this_session));
    
    // Every 5th save, show a congratulatory message
    if (total_saves_this_session % 5 == 0) {
        fm.notify(fmt("Great job! %d saves this session!", total_saves_this_session));
    }
    
    // Auto-backup for important files (example: .c and .h files)
    let ext = path_ext(path);
    if (ext == ".c" || ext == ".h") {
        fm.console("Auto-backup: Important C file saved - " + filename);
        // You could extend this to actually create a backup using fm.copy()
    }
}

fn on_editor_open(path) {
    save_count = 0;  // Reset per-file save counter
    fm.console("File opened: " + path);
    fm.console("This file hasn't been saved yet in this session");
}

// Helper: check if string ends with suffix (NO string indexing)
fn ends_with(text, suffix) {
    if (text == nil || suffix == nil) {
        return false;
    }
    if (typeof(text) != "string" || typeof(suffix) != "string") {
        return false;
    }

    let text_len = len(text);
    let suffix_len = len(suffix);

    if (suffix_len > text_len) {
        return false;
    }

    return substr(text, text_len - suffix_len, suffix_len) == suffix;
}

// Helper: split string by delimiter (NO string indexing)
fn split(text, delim) {
    if (text == nil || typeof(text) != "string") {
        return [];
    }
    if (delim == nil || typeof(delim) != "string" || len(delim) == 0) {
        return [text];
    }

    let result = [];
    let current = "";
    let delim_len = len(delim);

    let i = 0;
    while (i < len(text)) {
        // delimiter match via substr
        if (i + delim_len <= len(text) && substr(text, i, delim_len) == delim) {
            if (len(current) > 0) {
                result = result + [current];
                current = "";
            }
            i = i + delim_len;
        } else {
            // append one character via substr
            current = current + substr(text, i, 1);
            i = i + 1;
        }
    }

    if (len(current) > 0) {
        result = result + [current];
    }
    return result;
}

// Helper function to get slice of list
fn slice(list, start, end) {
    let result = [];
    for (let i = start; i < end && i < len(list); i = i + 1) {
        result = result + [list[i]];
    }
    return result;
}
