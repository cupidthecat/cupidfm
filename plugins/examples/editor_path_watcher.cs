// editor_path_watcher.cs
// Example plugin demonstrating fm.editor_get_path() usage
// This plugin tracks which file is open in the editor and provides utilities

let last_known_path = nil;

fn on_load() {
    fm.console("Editor path watcher plugin loaded");
    fm.console("Press ^P to see current editor path");
}

fn on_key(key) {
    // Ctrl+P: Show current editor path
    if (key == "^P") {
        let path = fm.editor_get_path();
        
        if (path == nil) {
            fm.popup("Editor Status", "No file is currently open in the editor");
        } else {
            let info = fmt("Current Editor File:\n\nFull path: %s", path);
            fm.popup("Editor Status", info);
        }
        return true;
    }
    
    // Ctrl+Y: Copy editor path to clipboard
    if (key == "^Y") {
        let path = fm.editor_get_path();
        
        if (path == nil) {
            fm.notify("No file open in editor");
        } else {
            fm.clipboard_set(path);
            fm.notify(fmt("Copied to clipboard: %s", path));
        }
        return true;
    }
    
    return false;
}



fn on_editor_open(path) {
    // Track when a new file is opened
    if (last_known_path != path) {
        last_known_path = path;
        fm.console(fmt("Editor opened new file: %s", path));
    }
}

