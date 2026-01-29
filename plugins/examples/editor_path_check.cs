// editor_path_check.cs
// Demonstrates fm.editor_get_path()

fn on_load() {
    fm.notify("Editor path check loaded (F9)");
    fm.bind("F9", "show_editor_path");
}

fn show_editor_path(key) {
    let p = fm.editor_get_path();
    if (p == nil || p == "") {
        fm.notify("Editor is not open");
    } else {
        fm.notify("Editor path: " + p);
    }
    return true;
}
