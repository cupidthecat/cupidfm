// CupidFM plugin example (CupidScript)
//
// Install:
//   mkdir -p ~/.cupidfm/plugins
//   cp plugins/example.cs ~/.cupidfm/plugins/
//
// Hooks:
//   fn on_load()
//   fn on_key(key) -> bool (return true to consume)
//   fn on_dir_change(new_cwd, old_cwd)
//   fn on_selection_change(new_name, old_name)
//

fn on_load() {
  fm.notify("example.cs loaded (CupidFM plugins)");
  fm.console_print("example.cs: loaded (open console with ^O)");
  // Demo binds:
  // - Ctrl+K: go to parent directory (relative cd)
  // - Ctrl+J: try to select README.md in the current directory
  fm.bind("^K", "go_parent");
  fm.bind("^J", "select_readme");
}

fn go_parent(key) {
  fm.console_print("go_parent: cd ..");
  fm.cd("..");
  return true;
}

fn select_readme(key) {
  fm.console_print("select_readme: selecting README.md");
  fm.select("README.md");
  return true;
}

fn on_key(key) {
  // Return false so CupidFM handles everything else normally.
  return false;
}

fn on_dir_change(new_cwd, old_cwd) {
  fm.status("dir: " + old_cwd + " -> " + new_cwd);
  fm.console_print("dir change: " + old_cwd + " -> " + new_cwd);
}

fn on_selection_change(new_name, old_name) {
  // Called frequently while browsing; keep it quiet by default.
}
