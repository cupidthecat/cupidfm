// editor_lines_demo.cs
// Example plugin demonstrating fm.editor_get_lines() usage
// Shows how to retrieve and work with specific ranges of lines

fn on_load() {
    fm.console("Editor lines demo plugin loaded");
    fm.console("Press ^L to see first 10 lines");
    fm.console("Press ^K to reverse selected line range");
}

fn on_key(key) {
    // Ctrl+L: Show first 10 lines
    if (key == "^L") {
        if (!fm.editor_active()) {
            fm.popup("Lines Preview", "No file is currently open in the editor");
            return true;
        }
        
        let total = fm.editor_line_count();
        let end = 10;
        if (total < end) {
            end = total;
        }
        
        let lines = fm.editor_get_lines(1, end);
        if (lines == nil) {
            fm.notify("Could not retrieve lines");
            return true;
        }
        
        // Build preview text
        let preview = fmt("First %d lines:\n\n", end);
        let line_num = 1;
        for line in lines {
            preview = preview + fmt("%d: %s\n", line_num, line);
            line_num = line_num + 1;
        }
        
        fm.popup("Lines Preview", preview);
        return true;
    }
    
    // Ctrl+K: Reverse lines in selection
    if (key == "^K") {
        if (!fm.editor_active()) {
            fm.notify("No file open in editor");
            return true;
        }
        
        let sel = fm.editor_get_selection();
        if (sel == nil) {
            fm.notify("No selection - select lines first");
            return true;
        }
        
        let start_line = sel["start_line"];
        let end_line = sel["end_line"];
        
        // Get the selected lines
        let lines = fm.editor_get_lines(start_line, end_line);
        if (lines == nil) {
            fm.notify("Could not retrieve lines");
            return true;
        }
        
        // Reverse the lines
        let reversed = [];
        let count = len(lines);
        for (let i = count - 1; i >= 0; i = i - 1) {
            push(reversed, lines[i]);
        }
        
        // Build replacement text (without trailing newline)
        let text = "";
        let i = 0;
        for line in reversed {
            text = text + line;
            if (i < count - 1) {
                text = text + "\n";
            }
            i = i + 1;
        }
        
        // Calculate character positions for replacement
        let start_col = 1;
        let end_col = len(lines[count - 1]) + 1;
        
        // Replace the selected lines with reversed text
        fm.editor_replace_text(start_line, start_col, end_line, end_col, text);
        
        fm.notify(fmt("Reversed %d lines", count));
        return true;
    }
    
    return false;
}

fn on_editor_open(path) {
    // Show line count when file is opened
    let total = fm.editor_line_count();
    if (total > 0) {
        fm.console(fmt("Opened: %s (%d lines)", path, total));
        
        // Show first line as preview
        let first_line = fm.editor_get_lines(1, 1);
        if (first_line != nil && len(first_line) > 0) {
            fm.console(fmt("First line: %s", first_line[0]));
        }
    }
}
