// line_count_demo.cs
// Example plugin demonstrating fm.editor_line_count() usage
// Shows how to track and display line count information

fn on_load() {
    fm.console("Line count demo plugin loaded");
    fm.console("Press ^N to see file statistics");
}

fn on_key(key) {
    // Ctrl+N: Show line count and navigation stats
    if (key == "^N") {
        if (!fm.editor_active()) {
            fm.popup("File Stats", "No file is currently open in the editor");
            return true;
        }
        
        let path = fm.editor_get_path();
        let total_lines = fm.editor_line_count();
        let cursor = fm.editor_get_cursor();
        
        if (cursor == nil) {
            fm.notify("Could not get cursor position");
            return true;
        }
        
        let current_line = cursor["line"];
        let progress = (current_line * 100) / total_lines;
        
        // Calculate lines remaining
        let lines_above = current_line - 1;
        let lines_below = total_lines - current_line;
        
        let stats = fmt("File: %s\n\n", path);
        stats = stats + fmt("Total Lines: %d\n", total_lines);
        stats = stats + fmt("Current Line: %d\n", current_line);
        stats = stats + fmt("Progress: %d%%\n\n", progress);
        stats = stats + fmt("Lines Above: %d\n", lines_above);
        stats = stats + fmt("Lines Below: %d", lines_below);
        
        fm.popup("File Statistics", stats);
        return true;
    }
    
    return false;
}

fn on_editor_open(path) {
    // Announce line count when opening files
    let count = fm.editor_line_count();
    
    // Provide feedback based on file size
    if (count == 0) {
        fm.console(fmt("Opened empty file: %s", path));
    } else if (count < 100) {
        fm.console(fmt("Opened small file: %s (%d lines)", path, count));
    } else if (count < 1000) {
        fm.console(fmt("Opened medium file: %s (%d lines)", path, count));
    } else {
        fm.console(fmt("Opened large file: %s (%d lines)", path, count));
    }
}
