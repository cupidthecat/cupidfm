// editor_state_check.cs
// Demonstrates checking if the built-in text editor is active

fn on_load() {
    fm.notify("Editor state monitor loaded (F10)");
    
    // Bind F10 to show editor status
    fm.bind("F10", "show_editor_status");
    
    // Hook that monitors editor state changes
    fm.on("change", fn() {
        if (fm.editor_active()) {
            fm.ui_status_set("✏️  EDITING");
        } else {
            fm.ui_status_clear();
        }
    });
    
    // Example: Prevent certain actions while editor is open
    fm.bind("Ctrl+D", "safe_delete");
}

fn show_editor_status(key) {
    if (fm.editor_active()) {
        fm.notify("✏️  Editor is currently OPEN");
    } else {
        fm.notify("📁 Editor is currently CLOSED");
    }
    return true;
}

fn safe_delete(key) {
    if (fm.editor_active()) {
        fm.notify("⚠️  Cannot delete while editor is open");
        return true;
    }
    
    // Proceed with delete
    fm.delete();
    return true;
}
