// hooks_status.cs - demonstrates on_dir_change/on_selection_change + maps + throttling
//
// Suggested binds:
//   F10 - toggle hook messages
//
// Install:
//   cp plugins/examples/hooks_status.cs ~/.cupidfm/plugins/

let st = map();
st["enabled"] = true;
st["last_ms"] = 0;

fn on_load() {
  fm.notify("hooks_status.cs loaded (F10 toggles)");
  fm.console_print("hooks_status.cs: loaded (F10 toggles)");
  fm.bind("F10", "toggle_hooks");
}

fn toggle_hooks(key) {
  st["enabled"] = !st["enabled"];
  fm.status("hooks enabled=" + fmt("%b", st["enabled"]));
  fm.console_print("hooks enabled=" + fmt("%b", st["enabled"]));
  return true;
}

fn throttle_ok() {
  let now = now_ms();
  let last = st["last_ms"];
  if (now - last < 200) {
    return false;
  }
  st["last_ms"] = now;
  return true;
}

fn on_dir_change(new_cwd, old_cwd) {
  if (!st["enabled"]) {
    return;
  }
  if (!throttle_ok()) {
    return;
  }
  fm.status(fmt("dir: %s -> %s", old_cwd, new_cwd));
  fm.console_print(fmt("dir: %s -> %s", old_cwd, new_cwd));
}

fn on_selection_change(new_name, old_name) {
  if (!st["enabled"]) {
    return;
  }
  if (!throttle_ok()) {
    return;
  }
  // Show extra context using APIs:
  fm.status(fmt("[%s] %d/%d  %s", fm.pane(), fm.cursor(), fm.count(), new_name));
  fm.console_print(fmt("[%s] %d/%d  %s", fm.pane(), fm.cursor(), fm.count(), new_name));
}
