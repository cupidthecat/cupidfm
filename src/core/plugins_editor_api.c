// plugins_editor_api.c
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "plugins_editor_api.h"

#include <stdbool.h>
#include <stdlib.h>

#include "../fs/files.h"
#include "cs_value.h"
#include "cs_vm.h"
#include "globals.h"

static int nf_fm_editor_active(cs_vm *vm, void *ud, int argc,
                               const cs_value *argv, cs_value *out) {
  (void)vm;
  (void)ud;
  (void)argc;
  (void)argv;
  if (out)
    *out = cs_bool(is_editing);
  return 0;
}

static int nf_fm_editor_get_path(cs_vm *vm, void *ud, int argc,
                                 const cs_value *argv, cs_value *out) {
  (void)ud;
  (void)argc;
  (void)argv;
  if (out) {
    if (is_editing && g_editor_path[0]) {
      *out = cs_str(vm, g_editor_path);
    } else {
      *out = cs_nil();
    }
  }
  return 0;
}

static int nf_fm_editor_save(cs_vm *vm, void *ud, int argc, const cs_value *argv,
                             cs_value *out) {
  (void)vm;
  (void)argv;
  PluginManager *pm = (PluginManager *)ud;
  if (argc != 0) {
    if (out)
      *out = cs_bool(false);
    return 0;
  }
  bool ok = editor_save_current(pm);
  if (out)
    *out = cs_bool(ok);
  return 0;
}

static int nf_fm_editor_save_as(cs_vm *vm, void *ud, int argc,
                                const cs_value *argv, cs_value *out) {
  (void)vm;
  PluginManager *pm = (PluginManager *)ud;
  if (argc != 1 || argv[0].type != CS_T_STR) {
    if (out)
      *out = cs_bool(false);
    return 0;
  }
  const char *path = cs_to_cstr(argv[0]);
  bool ok = editor_save_as(pm, path);
  if (out)
    *out = cs_bool(ok);
  return 0;
}

static int nf_fm_editor_close(cs_vm *vm, void *ud, int argc, const cs_value *argv,
                              cs_value *out) {
  (void)vm;
  (void)ud;
  (void)argv;
  if (argc != 0) {
    if (out)
      *out = cs_bool(false);
    return 0;
  }
  bool ok = editor_request_close();
  if (out)
    *out = cs_bool(ok);
  return 0;
}

static int nf_fm_editor_reload(cs_vm *vm, void *ud, int argc,
                               const cs_value *argv, cs_value *out) {
  (void)vm;
  (void)ud;
  (void)argv;
  if (argc != 0) {
    if (out)
      *out = cs_bool(false);
    return 0;
  }
  bool ok = editor_request_reload();
  if (out)
    *out = cs_bool(ok);
  return 0;
}

static int nf_fm_editor_set_readonly(cs_vm *vm, void *ud, int argc,
                                     const cs_value *argv, cs_value *out) {
  (void)vm;
  (void)ud;
  if (argc != 1 || argv[0].type != CS_T_BOOL) {
    if (out)
      *out = cs_bool(false);
    return 0;
  }
  bool ok = editor_set_readonly(argv[0].as.b != 0);
  if (out)
    *out = cs_bool(ok);
  return 0;
}

static int nf_fm_editor_get_content(cs_vm *vm, void *ud, int argc,
                                    const cs_value *argv, cs_value *out) {
  (void)ud;
  (void)argc;
  (void)argv;
  if (!out)
    return 0;
  char *content = editor_get_content_copy();
  if (!content) {
    *out = cs_nil();
    return 0;
  }
  *out = cs_str(vm, content);
  free(content);
  return 0;
}

static int nf_fm_editor_get_line(cs_vm *vm, void *ud, int argc,
                                 const cs_value *argv, cs_value *out) {
  (void)ud;
  if (!out)
    return 0;
  if (argc != 1 || argv[0].type != CS_T_INT) {
    *out = cs_nil();
    return 0;
  }
  int line_num = (int)argv[0].as.i;
  char *line = editor_get_line_copy(line_num);
  if (!line) {
    *out = cs_nil();
    return 0;
  }
  *out = cs_str(vm, line);
  free(line);
  return 0;
}

static int nf_fm_editor_get_lines(cs_vm *vm, void *ud, int argc,
                                  const cs_value *argv, cs_value *out) {
  (void)ud;
  if (!out)
    return 0;
  if (argc != 2 || argv[0].type != CS_T_INT || argv[1].type != CS_T_INT) {
    *out = cs_nil();
    return 0;
  }

  int start = (int)argv[0].as.i;
  int end = (int)argv[1].as.i;
  if (start < 1 || end < start) {
    *out = cs_nil();
    return 0;
  }

  cs_value listv = cs_list(vm);
  cs_list_obj *l = (cs_list_obj *)listv.as.p;
  if (!l) {
    *out = cs_nil();
    return 0;
  }

  for (int i = start; i <= end; i++) {
    char *line = editor_get_line_copy(i);
    if (!line) {
      *out = cs_nil();
      return 0;
    }
    (void)cs_list_push(listv, cs_str(vm, line));
    free(line);
  }

  *out = listv;
  return 0;
}

static int nf_fm_editor_line_count(cs_vm *vm, void *ud, int argc,
                                   const cs_value *argv, cs_value *out) {
  (void)vm;
  (void)ud;
  (void)argc;
  (void)argv;
  if (out)
    *out = cs_int(editor_get_line_count());
  return 0;
}

static int nf_fm_editor_get_cursor(cs_vm *vm, void *ud, int argc,
                                   const cs_value *argv, cs_value *out) {
  (void)ud;
  (void)argc;
  (void)argv;
  if (!out)
    return 0;

  int line = 0, col = 0;
  if (!editor_get_cursor(&line, &col)) {
    *out = cs_nil();
    return 0;
  }

  cs_value mapv = cs_map(vm);
  cs_map_set(mapv, "line", cs_int(line));
  cs_map_set(mapv, "col", cs_int(col));
  *out = mapv;
  return 0;
}

static int nf_fm_editor_set_cursor(cs_vm *vm, void *ud, int argc,
                                   const cs_value *argv, cs_value *out) {
  (void)vm;
  (void)ud;
  if (argc < 2)
    return -1;
  if (argv[0].type != CS_T_INT || argv[1].type != CS_T_INT) {
    return -1;
  }
  int line = (int)argv[0].as.i;
  int col = (int)argv[1].as.i;
  bool success = editor_set_cursor(line, col);
  if (out)
    *out = cs_bool(success);
  return 0;
}

static int nf_fm_editor_get_selection(cs_vm *vm, void *ud, int argc,
                                      const cs_value *argv, cs_value *out) {
  (void)ud;
  (void)argc;
  (void)argv;
  if (!out)
    return 0;

  int start_line = 0, start_col = 0, end_line = 0, end_col = 0;
  if (!editor_get_selection(&start_line, &start_col, &end_line, &end_col)) {
    *out = cs_nil();
    return 0;
  }

  cs_value mapv = cs_map(vm);
  cs_map_set(mapv, "start_line", cs_int(start_line));
  cs_map_set(mapv, "start_col", cs_int(start_col));
  cs_map_set(mapv, "end_line", cs_int(end_line));
  cs_map_set(mapv, "end_col", cs_int(end_col));
  *out = mapv;
  return 0;
}

static int nf_fm_editor_insert_text(cs_vm *vm, void *ud, int argc,
                                    const cs_value *argv, cs_value *out) {
  (void)vm;
  (void)ud;
  if (argc != 1 || argv[0].type != CS_T_STR) {
    if (out)
      *out = cs_bool(false);
    return 0;
  }
  const char *text = cs_to_cstr(argv[0]);
  bool result = editor_insert_text(text);
  if (out)
    *out = cs_bool(result);
  return 0;
}

static int nf_fm_editor_replace_text(cs_vm *vm, void *ud, int argc,
                                     const cs_value *argv, cs_value *out) {
  (void)vm;
  (void)ud;
  if (argc != 5 || argv[0].type != CS_T_INT || argv[1].type != CS_T_INT ||
      argv[2].type != CS_T_INT || argv[3].type != CS_T_INT ||
      argv[4].type != CS_T_STR) {
    if (out)
      *out = cs_bool(false);
    return 0;
  }
  int start_line = (int)argv[0].as.i;
  int start_col = (int)argv[1].as.i;
  int end_line = (int)argv[2].as.i;
  int end_col = (int)argv[3].as.i;
  const char *text = cs_to_cstr(argv[4]);

  bool result =
      editor_replace_text(start_line, start_col, end_line, end_col, text);
  if (out)
    *out = cs_bool(result);
  return 0;
}

static int nf_fm_editor_delete_range(cs_vm *vm, void *ud, int argc,
                                     const cs_value *argv, cs_value *out) {
  (void)vm;
  (void)ud;
  if (argc != 4 || argv[0].type != CS_T_INT || argv[1].type != CS_T_INT ||
      argv[2].type != CS_T_INT || argv[3].type != CS_T_INT) {
    if (out)
      *out = cs_bool(false);
    return 0;
  }

  int start_line = (int)argv[0].as.i;
  int start_col = (int)argv[1].as.i;
  int end_line = (int)argv[2].as.i;
  int end_col = (int)argv[3].as.i;

  bool result = editor_delete_range(start_line, start_col, end_line, end_col);
  if (out)
    *out = cs_bool(result);
  return 0;
}

static int nf_fm_editor_uppercase_selection(cs_vm *vm, void *ud, int argc,
                                            const cs_value *argv,
                                            cs_value *out) {
  (void)vm;
  (void)ud;
  (void)argc;
  (void)argv;
  if (!is_editing) {
    if (out)
      *out = cs_bool(false);
    return 0;
  }
  editor_apply_uppercase_to_selection();
  if (out)
    *out = cs_bool(true);
  return 0;
}

void plugins_register_editor_api(cs_vm *vm, PluginManager *pm) {
  cs_register_native(vm, "fm.editor_active", nf_fm_editor_active, pm);
  cs_register_native(vm, "fm.editor_get_path", nf_fm_editor_get_path, pm);
  cs_register_native(vm, "fm.editor_save", nf_fm_editor_save, pm);
  cs_register_native(vm, "fm.editor_save_as", nf_fm_editor_save_as, pm);
  cs_register_native(vm, "fm.editor_close", nf_fm_editor_close, pm);
  cs_register_native(vm, "fm.editor_reload", nf_fm_editor_reload, pm);
  cs_register_native(vm, "fm.editor_set_readonly", nf_fm_editor_set_readonly, pm);
  cs_register_native(vm, "fm.editor_get_content", nf_fm_editor_get_content, pm);
  cs_register_native(vm, "fm.editor_get_line", nf_fm_editor_get_line, pm);
  cs_register_native(vm, "fm.editor_get_lines", nf_fm_editor_get_lines, pm);
  cs_register_native(vm, "fm.editor_line_count", nf_fm_editor_line_count, pm);
  cs_register_native(vm, "fm.editor_get_cursor", nf_fm_editor_get_cursor, pm);
  cs_register_native(vm, "fm.editor_set_cursor", nf_fm_editor_set_cursor, pm);
  cs_register_native(vm, "fm.editor_get_selection", nf_fm_editor_get_selection, pm);
  cs_register_native(vm, "fm.editor_insert_text", nf_fm_editor_insert_text, pm);
  cs_register_native(vm, "fm.editor_replace_text", nf_fm_editor_replace_text, pm);
  cs_register_native(vm, "fm.editor_delete_range", nf_fm_editor_delete_range, pm);
  cs_register_native(vm, "fm.editor_uppercase_selection",
                     nf_fm_editor_uppercase_selection, pm);
}
