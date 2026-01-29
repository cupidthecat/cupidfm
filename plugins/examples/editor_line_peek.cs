// editor_line_peek.cs
// Demonstrates fm.editor_get_line()

fn on_load() {
    fm.notify("Editor line peek loaded (F7)");
    fm.bind("F7", "show_editor_line");
}

fn show_editor_line(key) {
    let ln = fm.prompt("Line number", "1");
    if (ln == nil || ln == "") { return true; }
    let n = to_int(ln);
    let line = fm.editor_get_line(n);
    if (line == nil) {
        fm.notify("No such line (or editor closed)");
        return true;
    }
    fm.popup("Line " + fmt("%v", n), line);
    return true;
}
