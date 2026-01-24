// CupidFM plugin example (CupidScript)
//
// Install:
//   mkdir -p ~/.cupidfm/plugins
//   cp plugins/example.cs ~/.cupidfm/plugins/
//
// Hooks:
//   function on_load()
//   function on_key(key) -> bool (return true to consume)
//

fn on_load() {
  fm.notify("example.cs loaded (CupidFM plugins)");
  // Bind Ctrl+K to our handler as a demo.
  fm.bind("^K", "on_ctrl_t");
}

fn on_ctrl_t(key) {
  fm.popup("Example Plugin", "Intercepted key: " + key + "\n\nSelected:\n" + fm.selected_path());
  return true;
}

fn on_key(key) {
  // Return false so CupidFM handles everything else normally.
  return false;
}
