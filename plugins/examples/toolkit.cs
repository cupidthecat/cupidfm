// toolkit.cs - demonstrates prompt/confirm/menu + file ops + undo/redo
//
// Suggested binds:
//   F7  - open menu
//
// Install:
//   mkdir -p ~/.cupidfm/plugins/lib
//   cp plugins/examples/toolkit.cs ~/.cupidfm/plugins/

fn on_load() {
  fm.notify("toolkit.cs loaded (F7)");
  fm.console("toolkit.cs: loaded (F7)");
  fm.bind("F7", "open_toolkit");
}

fn open_toolkit(key) {
  fm.console("toolkit: open menu");
  let items = list();
  push(items, "Rename selected");
  push(items, "Copy selected -> dst dir");
  push(items, "Move selected -> dst dir");
  push(items, "Delete selected (trash)");
  push(items, "Create dir (mkdir)");
  push(items, "Create file (touch)");
  push(items, "Undo");
  push(items, "Redo");

  let idx = fm.menu("Toolkit", items);
  if (idx < 0) {
    return true;
  }
  fm.console("toolkit: menu idx=" + fmt("%d", idx));

  let sel = fm.selected_path();
  if ((idx < 4) && (sel == "")) {
    fm.status("No selection");
    fm.console("toolkit: no selection");
    return true;
  }

  if (idx == 0) {
    let new_name = fm.prompt("Rename", fm.selected_name());
    if (new_name != nil && new_name != "") {
      fm.rename(sel, new_name);
    }
  } else {
    if (idx == 1) {
      let dst = fm.prompt("Copy to directory", fm.cwd());
      if (dst != nil && dst != "") {
        fm.copy(sel, dst);
      }
    } else {
      if (idx == 2) {
        let dst = fm.prompt("Move to directory", fm.cwd());
        if (dst != nil && dst != "") {
          fm.move(sel, dst);
        }
      } else {
        if (idx == 3) {
          let ok = fm.confirm("Delete", "Trash:\n" + sel + "\n\nConfirm?");
          if (ok) {
            fm.delete(sel);
          }
        } else {
          if (idx == 4) {
            let name = fm.prompt("mkdir (name or path)", "new_dir");
            if (name != nil && name != "") {
              fm.mkdir(name);
            }
          } else {
            if (idx == 5) {
              let name = fm.prompt("touch (name or path)", "new_file.txt");
              if (name != nil && name != "") {
                fm.touch(name);
              }
            } else {
              if (idx == 6) {
                fm.undo();
              } else {
                if (idx == 7) {
                  fm.redo();
                }
              }
            }
          }
        }
      }
    }
  }

  return true;
}
