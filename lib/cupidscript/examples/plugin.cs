fn on_load() {
  fm.status("plugin loaded");
}

fn on_key(key) {
  if (key == "F5") {
    fm.open(fm.selected_path());
  }
}

