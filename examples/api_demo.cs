// api_demo.cs - exercises the new CupidScript `fm.entries()`, navigation, and search APIs
//
// Suggested binds:
//   F8  - log the first few entries (uses `fm.console`)
//   F9  - set/clear the fuzzy search filter via `fm.set_search`
//   F10 - clear the search filter explicitly
//   F11 - open the selected entry (`fm.open_selected`)
//   F12 - jump between parent/child directories (`fm.parent_dir`/`fm.enter_dir`)
//
// Install:
//   cp plugins/examples/api_demo.cs ~/.cupidfm/plugins/

fn on_load() {
  fm.notify("api_demo.cs loaded (F8 entries, F9 search, F10 clear, F11 open, F12 nav)");
  fm.console_print("api_demo.cs: loaded (uses fm.console_print+entries+search helpers)");
  fm.bind("F8", "log_entries");
  fm.bind("F9", "set_or_toggle_search");
  fm.bind("F10", "clear_search");
  fm.bind("F11", "open_selected_entry");
  fm.bind("F12", "parent_or_child");
}

fn log_entries(key) {
  let entries = fm.entries();
  let total = len(entries);
  // Some APIs may return int(0/1) instead of bool; coerce via comparison.
  let active = fm.search_active() != 0;
  fm.console_print(fmt("api_demo: entries=%d search_active=%b query=%v",
                 total, active, fm.search_query()));

  let limit = total;
  if (limit > 5) {
    limit = 5;
  }

  let idx = 0;
  while (idx < limit) {
    let entry = entries[idx];
    let name = entry["name"];
    let size = entry["size"];
    // Coerce potential int(0/1) into bool for fmt("%b").
    let is_dir = entry["is_dir"] != 0;
    let mime = entry["mime"];
    // Use %v so nil/unknown values don't crash fmt("%s").
    fm.console_print(fmt("[%d] %v dir=%b size=%d mime=%v",
                   idx, name, is_dir, size, mime));
    idx = idx + 1;
  }
  return true;
}

fn set_or_toggle_search(key) {
  let current = fm.search_query();
  if (current == "README") {
    fm.clear_search();
    fm.console_print("api_demo: search cleared (README cycle)");
    return true;
  }
  let q = fm.prompt("Enter search query (empty clears)", current);
  if (q == nil) {
    return true;
  }
  if (q == "") {
    fm.clear_search();
    fm.console_print("api_demo: search cleared via prompt");
    return true;
  }
  if (fm.set_search(q)) {
    fm.console_print("api_demo: search set to " + q);
  } else {
    fm.console_print("api_demo: failed to set search");
  }
  return true;
}

fn clear_search(key) {
  if (fm.clear_search()) {
    fm.console_print("api_demo: search cleared (F10)");
  } else {
    fm.console_print("api_demo: no active search to clear");
  }
  return true;
}

fn open_selected_entry(key) {
  fm.console_print("api_demo: open_selected()");
  fm.open_selected();
  return true;
}

fn parent_or_child(key) {
  if (fm.enter_dir()) {
    fm.console_print("api_demo: entered child directory");
    return true;
  }
  if (fm.parent_dir()) {
    fm.console_print("api_demo: moved to parent directory");
    return true;
  }
  fm.console_print("api_demo: navigation request had no effect");
  return true;
}
