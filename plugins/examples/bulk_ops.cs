// bulk_ops.cs - demonstrates list/map + require + bulk copy/move/delete
//
// Suggested binds:
//   F8  - bulk operations prompt
//
// Install:
//   mkdir -p ~/.cupidfm/plugins/lib
//   cp plugins/examples/bulk_ops.cs ~/.cupidfm/plugins/
//   cp plugins/examples/lib/util.cs ~/.cupidfm/plugins/lib/

require("./lib/util.cs");

let state = map();
state["last_op"] = "none";

fn on_load() {
  fm.notify("bulk_ops.cs loaded (F8)");
  fm.console_print("bulk_ops.cs: loaded (F8)");
  fm.bind("F8", "bulk_dialog");
}

fn bulk_dialog(key) {
  fm.console_print("bulk_ops: open dialog");
  let default_paths = fm.selected_path();
  if (default_paths == "") {
    default_paths = "a.txt,b.txt";
  }

  let s = fm.prompt("Bulk paths (CSV, no spaces)", default_paths);
  if (s == nil) {
    return true;
  }
  let paths = list_from_csv(s);
  if (len(paths) == 0) {
    fm.status("No paths provided");
    fm.console_print("bulk_ops: no paths provided");
    return true;
  }

  let ops = list();
  push(ops, "Copy -> dst dir");
  push(ops, "Move -> dst dir");
  push(ops, "Delete (trash)");
  push(ops, "Undo");
  push(ops, "Redo");

  let idx = fm.menu("Bulk operation", ops);
  if (idx < 0) {
    return true;
  }
  fm.console_print("bulk_ops: menu idx=" + fmt("%d", idx));

  if (idx == 0 || idx == 1) {
    let dst = fm.prompt("Destination directory", fm.cwd());
    if (dst == nil || dst == "") {
      return true;
    }
    if (idx == 0) {
      fm.copy(paths, dst);
      state["last_op"] = "copy";
    } else {
      fm.move(paths, dst);
      state["last_op"] = "move";
    }
  } else {
    if (idx == 2) {
      let ok = fm.confirm("Delete", fmt("Trash %d item(s)?", len(paths)));
      if (ok) {
        fm.delete(paths);
        state["last_op"] = "delete";
      }
    } else {
      if (idx == 3) {
        fm.undo();
      } else {
        if (idx == 4) {
          fm.redo();
        }
      }
    }
  }

  fm.status("last_op=" + state["last_op"]);
  fm.console_print("bulk_ops: last_op=" + state["last_op"]);
  return true;
}
