# CupidFM Cupidscript API (with New Features)

CupidFM supports plugin scripting via [CupidScript](#cupidscript), a lightweight, embeddable scripting language and VM written in C99. This document details plugin loading, the plugin architecture, the `fm.*` API exposed to scripts in CupidFM, and a comprehensive overview of the latest CupidScript language features.

---

# CupidScript

CupidScript is a compact, embeddable scripting VM (C99), designed for fast integration. It provides a simple C API for host embedding, plugin scripting, and seamless extension with native functions.

---

## What's Included

- **Core runtime:** Lexer, parser, AST, virtual machine, and a minimal standard library.
- **Sample CLI and main (`src/main.c`):** Demonstrates embedding, native API registration (e.g., `fm.*`).
- **Public headers** for C API (`src/cupidscript.h`, etc).
- **Examples/tests** covering all language features, host extension, and API use.

---

## Language Overview

CupidScript: small, dynamic, with a modern feature set. Tree-walk interpreter; strong runtime error reporting; simple types.

### Syntax Example

```cs
let name = expr;           // declaration
name = expr;               // assignment

fn add(a, b) { return a + b; }

if (cond) { ... } else { ... }
while (cond) { ... }
return expr;
```
- **Operators:** `||`, `&&`, `==`, `!=`, `<`, `<=`, `>`, `>=`, `+`, `-`, `*`, `/`, `%`, unary `!`, `-`
- **Types/Values:** `nil`, `true`/`false`, int, float, string, list, map, strbuf, function, native

### Recent Language Features

- **Anonymous Functions & Closures**
  ```cs
  let double = fn(x) { return x*2; };
  let add_fn = fn(a,b) { return a+b; };
  fn make_counter() {
    let n = 0;
    return fn() { n=n+1; return n; };
  }
  ```
- **First-Class Functions:** Pass/return/assign functions; store in containers.
- **Short-Circuit Logic:** `&&`, `||`
- **String Concatenation:** Use `+`, e.g., `"foo" + 123`
- **Modern Errors:** Stack traces, rich source location.
- **Indexed map access (`m[k]`), map key querying (`keys(m)`)**
- **Optional trailing commas in lists/maps**
- **Improved error/stack reporting, with precise line/col info**
- **Unpack syntax for lists:**  
  ```cs
  let [a, b, ...rest] = arr;
  ```
- **Top-level await** (if host uses async)
- **`typeof` returns detailed type names ("native", "function", etc)**
- **String interpolation**:  
  ```cs
  print("The value is: $(x)");
  ```
- **`fmt` supports more specifiers (`%b`, `%v` etc)**
- **`assert_eq`, `assert_ne` (testing stdlib)**

#### Core language features (per CupidScript wiki)

These are the *core* language/stdlib behaviors implemented by the current lexer/parser/VM as documented in the CupidScript wiki.

- **List and map literals**
  ```cs
  let xs = [1, 2, 3];
  let m = {"name": "Frank", "age": 30};
  ```
- **Map field access sugar** (maps only)
  ```cs
  let m = {"name": "Frank"};
  print(m.name);     // same as m["name"]
  ```
  If the value is *not* a map, `obj.field` is a runtime error.

- **`for ... in` loops** (iterate lists; maps iterate keys)
  ```cs
  for x in [10, 20, 30] {
    print(x);
  }
  for k in keys({"a": 1, "b": 2}) {
    print(k);
  }
  ```

- **C-style `for (init; cond; incr)` loops**
  ```cs
  for (i = 0; i < 10; i = i + 1) {
    print(i);
  }
  ```

- **Range operator**
  ```cs
  let nums = 0..5;    // [0,1,2,3,4]
  let incl = 0..=5;   // [0,1,2,3,4,5]
  for i in 1..=3 { print(i); }
  ```
  Ranges work in both directions (ascending/descending) automatically.

- **Ternary expression**
  ```cs
  let max = a > b ? a : b;
  ```

- **Exceptions**: `throw` and `try/catch`
  ```cs
  try {
    throw "boom";
  } catch (e) {
    print("caught:", e);
  }
  ```

- **Standardized error objects**: `error`, `is_error`, `format_error`, global `ERR`
  ```cs
  try {
    throw error("Division by zero", "DIV_ZERO");
  } catch (e) {
    print(format_error(e));
  }
  ```

---

## Built-In Types: Lists and Maps

Lists and maps are built-in, dynamic and mutable.

```cs
let xs = list();
push(xs, 10);
push(xs, 20);
xs[1] = 99;
print(xs[0], xs[1], len(xs)); // 10 99 2

let m = map();
m["answer"] = 42;
print(m["answer"], keys(m)); // 42 ["answer"]
```
- Lists index by integer; maps index by string.
- Assign as `xs[i]=`, `m["key"]=value`
- Use `keys(map)` to enumerate (as a list of strings).
- Maps: string keys only. Lists: integer indices only.

Additional rules (per wiki):

- List indexing: index must be an `int`; negative or out-of-range returns `nil`.
- Map indexing: key must be a `string`; missing keys return `nil`.

## Control Flow (Additional)

CupidScript supports:

- `break;` / `continue;` inside `while`, `for ... in`, and C-style `for` loops.
- `return;` / `return expr;` from inside any block.

## Truthiness (Reminder)

Used by `if`, `while`, `!`, `&&`, `||`:

- `nil` is false
- `bool` is its value
- everything else is true (including `0`)

---

## Multi-File Scripts

- `load("path")` — always executes (like `#include`)
- `require("path")` — only executes the first time (like JS `require`)
- **New**: Loads use per-VM cache; relative to current file/script.

Additional module/path behavior (per wiki):

- The VM maintains a **current-directory stack** so relative module loads resolve relative to the calling script.
- `require_optional("path")` behaves like `require()`, but returns `nil` if the file is missing.
- `require()` returns a module **exports** map. Inside a required file, these globals are available:
  - `exports` (map)
  - `__file__` (string, resolved module path)
  - `__dir__` (string, directory containing the module)

---

## Directory Overview

- `src/cs_value.c`, `src/cs_lexer.c`, `src/cs_parser.c`, `src/cs_vm.c`, `src/cs_stdlib.c` – core runtime, VM, and stdlib
- `src/main.c` – CLI/embedding demo and entry point
- `src/cupidscript.h` (embedding API header)
- Build system: `Makefile`
---

## Build Instructions

**Requires:** C99 compiler (gcc/clang), POSIX tools.

### With `make`:

```sh
make all
```
Outputs:  
- `bin/libcupidscript.a` (static library)  
- `bin/cupidscript` (CLI interpreter)

### Manual:

See README for explicit build commands if not using make.

---

## Using CupidScript in C Hosts

Typical embedding workflow:

1. Create and configure VM:
   ```c
   cs_vm* vm = cs_vm_new();
   cs_register_stdlib(vm);
   cs_register_native(vm, "fm.notify", my_cb, NULL);
   ```
2. Execute scripts:
   ```c
   cs_vm_run_file(vm, "script.cs");
   ```
3. Invoke function from C:
   ```c
   cs_call(vm, "myfunc", argc, argv, &out);
   ```
4. Query last error (as string):
   ```c
   const char* err = cs_vm_last_error(vm);
   ```
Refer to the header for refcounted string/lists API, type checking, etc.

---

## CupidFM Plugins

CupidFM exposes a native scripting API (`fm.*`) to plugins via CupidScript.
Place `.cs` plugin scripts in designated directories and CupidFM will autoload them at startup.

### Plugin Locations

Load order (per session):

1. `~/.cupidfm/plugins`
2. `~/.cupidfm/plugin` (legacy)
3. `./cupidfm/plugins`
4. `./cupidfm/plugin` (legacy)
5. `./plugins`

All `.cs` scripts in these are loaded.

---

## Plugin Structure, Events & Lifecycle

Plugins = Any CupidScript `.cs` file with optional hooks:

- `fn on_load()` — after file loads (like init)
- `fn on_key(key)` — after keypress (return true/false to block/pass)
- `fn on_dir_change(new_cwd, old_cwd)` — when the panel dir changes
- `fn on_selection_change(new_name, old_name)` — selection changed

**New:**
- **Plugin API can export additional custom entry points.**
- **Error in hook?** Stacktrace with file/line/col will display in notification area.

---

## Key Format and Helpers

Keys use **string names**, e.g.:

- `"^T"` = Ctrl+T
- `"F5"`
- `"KEY_UP"` / `"KEY_DOWN"`
- `"Tab"`
- Any printable char (e.g., `"a"`)

Helpers for mapping:

- `fm.key_code(name)` — string→int
- `fm.key_name(code)` — int→string

---

## fm.* API (Plugin Scripting API)

CupidFM hosts these native functions for scripts:

### UI

- `fm.notify(msg)`
- `fm.status(msg)` (**alias**)
- `fm.popup(title, msg)`
- `fm.console_print(msg)` / `fm.console(msg)`
  - Append a line to CupidFM's in-app console log (open with `key_console`, default `^O`).
- `fm.prompt(title, initial) -> string|nil`
  - Modal input box. Returns the entered string, or `nil` if cancelled.
- `fm.confirm(title, msg) -> bool`
  - Modal yes/no box.
- `fm.menu(title, items) -> int`
  - Modal menu. `items` is a list of strings. Returns selected index, or `-1` if cancelled.

Async variants (callback-based):
- `fm.prompt_async(title, initial, cb) -> bool`
- `fm.confirm_async(title, msg, cb) -> bool`
- `fm.menu_async(title, items, cb) -> bool`
  - Queues a modal UI action and calls `cb(result)` after it completes.
  - `cb` may be a function value OR a function name string.

### Query Context

- `fm.cwd()` — current directory (left pane)
- `fm.selected_name()` — selection (filename or `""`)
- `fm.selected_path()`
- `fm.entries() -> list`
  - Returns the current visible directory listing (search-filtered when `fm.search_active()` is true).
  - Each entry is a map: `{name,is_dir,size,mtime,mode,mime}`.
- `fm.cursor()` — index or -1
- `fm.count()` — count of files/items
- `fm.search_active()` — true if search open
- `fm.search_query()`
- `fm.pane()` — string ("directory" or "preview")

### Search Control

(Async: these actions run **after** script hook completes.)

- `fm.set_search(query) -> bool`
  - Sets CupidFM's fuzzy filter/search query. Passing `""` clears search.
- `fm.clear_search() -> bool`
  - Clears CupidFM's fuzzy filter/search query.

### Key Bindings & Events

- `fm.bind(key, func_name)` — bind a key to your function (fn receives key string, return true to block)
- `fm.key_name(code)`
- `fm.key_code(name)`

### UI & System Control

- `fm.reload()` — refresh/reload panel UI
- `fm.exit()` — exit CupidFM

### File Navigation/Selection

(Async: these actions run **after** script hook completes.)

- `fm.cd(path)`
- `fm.select(name)`
- `fm.select_index(i)`
- `fm.open_selected() -> bool`
  - Opens the selected entry (enters directory if `is_dir`, otherwise opens editor for the selected file).
- `fm.enter_dir() -> bool`
  - Enters the selected directory (no-op if selection is not a directory).
- `fm.parent_dir() -> bool`
  - Navigates to the parent directory.

### File Operations + Undo Integration

(Async: these actions run **after** script hook completes.)

These operations are applied by CupidFM and recorded in CupidFM's undo stack, so plugin-triggered
actions work with `fm.undo()` / `fm.redo()`.

Path args:
- For `path`, you can pass either a single string OR a list of strings.
- Relative paths are resolved under `fm.cwd()`.

- `fm.copy(path, dst_dir) -> bool`
  - Copy files/dirs into `dst_dir` (destination name uses the source basename).
  - `path` may be a string or a list of strings.

- `fm.move(path, dst_dir) -> bool`
  - Move files/dirs into `dst_dir`.
  - `path` may be a string or a list of strings.

- `fm.rename(path, new_name) -> bool`
  - Rename/move a single path.
  - If `new_name` is relative, it stays in the same parent dir as `path`.
  - If `new_name` is absolute, it is used as the full destination path.

- `fm.delete(path) -> bool`
  - Soft-delete by moving into CupidFM's per-session trash (undoable).
  - `path` may be a string or a list of strings.

- `fm.mkdir(name_or_path) -> bool`
  - Create a directory (relative paths are under `fm.cwd()`).

- `fm.touch(name_or_path) -> bool`
  - Create an empty file (relative paths are under `fm.cwd()`).

Bulk helpers:
- `fm.selected_paths() -> list`
  - Returns a list of selected paths (currently at most one: the current selection).

- `fm.each_selected(fn_or_name)`
  - Calls your function once per selected path (currently at most once).
  - `fn_or_name` can be a function value OR the name of a function as a string.

Undo/redo:
- `fm.undo() -> bool`
- `fm.redo() -> bool`

---

## Minimal Example Plugin

Save as `~/.cupidfm/plugins/example.cs`:

```cs
fn on_load() {
  fm.notify("plugin loaded!");
  fm.bind("^K", "go_parent");
  fm.bind("^J", "select_readme");
  fm.bind("^D", "trash_selected");
}

fn go_parent(key) {
  fm.parent_dir();
  return true;
}

fn select_readme(key) {
  fm.select("README.md");
  return true;
}

fn trash_selected(key) {
  fm.delete(fm.selected_path());
  return true;
}

fn on_key(key) {
  return false; // let CupidFM handle
}

fn on_dir_change(new_cwd, old_cwd) {
  fm.status(fmt("dir: %s -> %s", old_cwd, new_cwd));
}

// optional:
fn on_selection_change(new_name, old_name) {
  // handle selection moved
}
```

### Example: API demo plugin

File: `plugins/examples/api_demo.cs`

- Logs the first few results from `fm.entries()` with `fm.console`.
- Drives fuzzy search from scripts via `fm.set_search(query)` and `fm.clear_search()`.
- Drives navigation using `fm.open_selected()`, `fm.enter_dir()`, and `fm.parent_dir()`.
- Binds the console-friendly commands to F8–F12 so you can "stress-test" the new helpers.

---

## Notes & Limitations

- Plugins run in their own VMs, synchronously.
- Side effects (cd, select) happen **after** event/handler returns.
- Errors/exceptions: shown in the notification bar; full stack traces included.
- API is intentionally minimal; expect (and request!) expansion.

---

## CupidScript Standard Library & New Features

Registered by calling `cs_register_stdlib(vm)`:

- **Core:** `print`, `assert`, `assert_eq`, `assert_ne`, `typeof`, `getenv`
- **Lists/Maps:** `list`, `map`, `len`, `push`, `pop`, `mget`, `mset`, `mhas`, `keys`
- **Lists/Maps (additional):** `insert`, `remove`, `slice`, `values`, `items`, `map_values`, `mdel`
- **Copy helpers:** `copy`, `deepcopy`
- **List helpers:** `reverse`, `reversed`, `contains`
- **String utils:** `str_find`, `str_replace`, `str_split`, string interpolation `$(...)`
- **String utils (additional):** `str_trim`, `str_ltrim`, `str_rtrim`, `str_lower`, `str_upper`, `str_startswith`, `str_endswith`, `str_repeat`
- **Path utils:** `path_join`, `path_dirname`, `path_basename`, `path_ext`
- **Formatting:** `fmt` supports `%d`, `%s`, `%b`, `%v`, `%%`
- **Time/helpers:** `now_ms`, `sleep`
- **Multi-file:** `load(path)`, `require(path)`, `require_optional(path)`
- **Error handling:** `error`, `is_error`, `format_error`, global `ERR`
- **Test helpers:** `assert_eq`, `assert_ne`
- **New features:**
    - Anonymous functions/closures
    - First-class functions (can pass/store/call)
    - Precise error stacktraces with file:line:col
    - String refcounting (safe for hosting APIs)
    - Top-level `await` support (host opt-in)
    - Plugin state via lists/maps
    - Async actions (navigation, selection happen after handlers)
    - Host can register arbitrary new natives (see API)

---

## Error Reporting

Parse/runtime errors provide `file:line:col` and a **full stacktrace**:

```text
Runtime error at examples/stacktrace.cs:3:15: division by zero
Stack trace:
  at inner (examples/stacktrace.cs:8:16)
  at outer (examples/stacktrace.cs:11:7)
```
`cs_vm_last_error(vm)` gives the last error message.

## Host Safety Controls (CPU/timeout limits)

CupidScript includes safety controls to keep scripts from hanging the host.

Host-level (C API) examples:

- `cs_vm_set_instruction_limit(vm, N)`
- `cs_vm_set_timeout(vm, ms)`
- `cs_vm_interrupt(vm)`

Script-level helpers (per wiki):

- `set_instruction_limit(n)` / `get_instruction_limit()` / `get_instruction_count()`
- `set_timeout(ms)` / `get_timeout()`

---

## Embedding & C Native Extensions

To add new host→script functions (native API):

```c
static int my_native(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
  // check, return result via *out (if used)
  if (out) *out = cs_nil();
  return 0; // return nonzero or use cs_error() on error
}
cs_register_native(vm, "my.native", my_native, NULL);
```
You may pass/retain `cs_value` (function refs, data) between script and C.

---

## CupidScript API Reference/Quick Snapshots

Types: `nil`, `bool`, `int`, `float`, `string`, `list`, `map`, `strbuf`, `function`, `native function`

- **Lists/maps:** `list`, `push`, `pop`, `insert`, `remove`, `slice`, `reverse`, `reversed`, `contains`, `copy`, `deepcopy`
- **Maps:** `map`, `mget`, `mset`, `mhas`, `mdel`, `keys`, `values`, `items`, `map_values`
- **Strings:** `"..."` (with `\n`, `\t`, `\\`, `\"` escapes), concatenation, indexing
- **Control flow:** `for x in xs {}`, `for (init; cond; incr) {}`, ranges `0..10` / `0..=10`, ternary `c ? a : b`
- **Errors:** `throw`, `try/catch`, `error`, `is_error`, `format_error`, global `ERR`
- **Functions/closures:**  
  ```cs
  fn make_counter() {
    let n = 0;
    return fn() { n=n+1; return n; };
  }
  ```
- **String interpolation:** `"count: $(n)"` (inject vars into strings)
- **List/map destructuring:** `let [a,b,...rest] = xs;` (new)
- **Top-level await:** (host opt-in) `await sleep(100);` (new)

---

## Design Notes

- Compact and fast stack/call VM.
- All plugin natives registered via public C API.
- String memory is refcounted.
- Plugin VMs are fully isolated (no data sharing by default).
- Modern scripting features and error handling.

---

## License

CupidFM and CupidScript are licensed under the GNU General Public License v3.  
See [`LICENSE`](LICENSE) or https://www.gnu.org/licenses/gpl-3.0.html for details.

---
