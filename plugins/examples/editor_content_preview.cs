// editor_content_preview.cs
// Demonstrates fm.editor_get_content()

fn on_load() {
    fm.notify("Editor content preview loaded (F8)");
    fm.bind("F8", "show_editor_content");
}

fn show_editor_content(key) {
    let text = fm.editor_get_content();
    if (text == nil || text == "") {
        fm.notify("Editor is not open or buffer is empty");
        return true;
    }
    // Show a short preview (first 200 chars)
    let preview = substr(text, 0, 200);
    if (len(text) > 200) {
        preview = preview + "...";
    }
    fm.popup("Editor Content", preview);
    return true;
}
