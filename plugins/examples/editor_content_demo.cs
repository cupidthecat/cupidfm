// editor_content_demo.cs
// Example plugin demonstrating fm.editor_get_content() usage
// Shows how to retrieve and analyze the full content of the editor buffer

fn on_load() {
    fm.console("Editor content demo plugin loaded");
    fm.console("Press ^W to see word count of current file");
    fm.console("Press ^F to find text in editor content");
}

fn on_key(key) {
    // Ctrl+W: Show word count statistics
    if (key == "^W") {
        let content = fm.editor_get_content();
        
        if (content == nil) {
            fm.popup("Word Count", "No file is currently open in the editor");
            return true;
        }
        
        // Count lines, words, and characters
        let lines = fm.editor_line_count();
        let chars = len(content);
        
        // Simple word count (split by whitespace)
        let words = 0;
        let in_word = false;
        for (let i = 0; i < chars; i = i + 1) {
            let ch = substr(content, i, 1);
            if (ch == " " || ch == "\n" || ch == "\t") {
                in_word = false;
            } else {
                if (!in_word) {
                    words = words + 1;
                    in_word = true;
                }
            }
        }
        
        let stats = fmt("File Statistics:\n\nLines: %d\nWords: %d\nCharacters: %d", 
                        lines, words, chars);
        fm.popup("Word Count", stats);
        return true;
    }
    
    // Ctrl+F: Simple text search in content
    if (key == "^F") {
        let content = fm.editor_get_content();
        
        if (content == nil) {
            fm.notify("No file open in editor");
            return true;
        }
        
        // Prompt for search text
        let search_term = fm.prompt("Find:", "");
        if (search_term == nil || search_term == "") {
            return true;
        }
        
        // Find occurrences using manual substring search
        let count = 0;
        let pos = 0;
        let search_len = len(search_term);
        let content_len = len(content);
        
        while (pos <= content_len - search_len) {
            // Check if substring matches at current position
            let matches = true;
            for (let i = 0; i < search_len; i = i + 1) {
                if (substr(content, pos + i, 1) != substr(search_term, i, 1)) {
                    matches = false;
                    break;
                }
            }
            
            if (matches) {
                count = count + 1;
                pos = pos + search_len;
            } else {
                pos = pos + 1;
            }
        }
        
        if (count > 0) {
            fm.console(fmt("Found %d occurrence(s) of '%s'", count, search_term));
            fm.notify(fmt("Found %d occurrence(s) of '%s'", count, search_term));
        } else {
            fm.notify(fmt("'%s' not found", search_term));
        }
        return true;
    }
    
    return false;
}

fn on_editor_open(path) {
    // Show a preview of the content when file is opened
    let content = fm.editor_get_content();
    if (content != nil) {
        let lines = fm.editor_line_count();
        let chars = len(content);
        fm.console(fmt("Opened: %s (%d lines, %d chars)", path, lines, chars));
    }
}
