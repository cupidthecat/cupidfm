// async_ui_demo.cs - demonstrates callback-based async prompt/confirm/menu
//
// Suggested binds:
//   F9  - open async menu
//
// Install:
//   cp plugins/examples/async_ui_demo.cs ~/.cupidfm/plugins/

fn on_load() {
  fm.notify("async_ui_demo.cs loaded (F9)");
  fm.console_print("async_ui_demo.cs: loaded (F9)");
  fm.bind("F9", "open_async_menu");
}

fn open_async_menu(key) {
  fm.console_print("async_ui_demo: open async menu");
  let items = list();
  push(items, "Rename selected");
  push(items, "mkdir (under cwd)");
  push(items, "touch (under cwd)");
  push(items, "Delete selected (trash)");
  push(items, "Undo");
  push(items, "Redo");
  fm.menu_async("Async Menu", items, on_menu_choice);
  return true;
}

fn on_menu_choice(idx) {
  fm.console_print("async_ui_demo: menu idx=" + fmt("%d", idx));
  if (idx < 0) {
    return;
  }

  if (idx == 0) {
    if (fm.selected_path() == "") {
      fm.status("No selection");
      return;
    }
    fm.prompt_async("Rename", fm.selected_name(), on_rename_value);
  } else {
    if (idx == 1) {
      fm.prompt_async("mkdir", "new_dir", on_mkdir_value);
    } else {
      if (idx == 2) {
        fm.prompt_async("touch", "new_file.txt", on_touch_value);
      } else {
        if (idx == 3) {
          if (fm.selected_path() == "") {
            fm.status("No selection");
            return;
          }
          fm.confirm_async("Delete", "Trash:\n" + fm.selected_path() + "\n\nConfirm?", on_delete_confirm);
        } else {
          if (idx == 4) {
            fm.undo();
          } else {
            if (idx == 5) {
              fm.redo();
            }
          }
        }
      }
    }
  }
}

fn on_rename_value(v) {
  fm.console_print("async_ui_demo: rename value=" + fmt("%v", v));
  if (v == nil || v == "") {
    return;
  }
  fm.rename(fm.selected_path(), v);
}

fn on_mkdir_value(v) {
  fm.console_print("async_ui_demo: mkdir value=" + fmt("%v", v));
  if (v == nil || v == "") {
    return;
  }
  fm.mkdir(v);
}

fn on_touch_value(v) {
  fm.console_print("async_ui_demo: touch value=" + fmt("%v", v));
  if (v == nil || v == "") {
    return;
  }
  fm.touch(v);
}

fn on_delete_confirm(ok) {
  fm.console_print("async_ui_demo: delete confirm=" + fmt("%b", ok));
  if (ok) {
    fm.delete(fm.selected_path());
  }
}
