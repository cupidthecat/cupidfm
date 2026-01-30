
# CupidFM Cupidscript API (with New Features)

---

## Table of Contents

- [CupidScript](#cupidscript)
  - [What's Included](#whats-included)
  - [Language Overview](#language-overview)
    - [Syntax Example](#syntax-example)
    - [Modern Language Features](#modern-language-features)
    - [Recent Language Features](#recent-language-features)
    - [Core language features (per CupidScript wiki)](#core-language-features-per-cupidscript-wiki)
- [Built-In Types](#built-in-types)
  - [Core Types](#core-types)
  - [Lists](#lists)
  - [Maps](#maps)
  - [Sets](#sets)
  - [Tuples](#tuples)
  - [Bytes](#bytes)
  - [Range](#range)
- [Control Flow (Additional)](#control-flow-additional)
- [Truthiness (Reminder)](#truthiness-reminder)
- [Multi-File Scripts](#multi-file-scripts)
- [Advanced Features](#advanced-features)
  - [Data Formats](#data-formats)
    - [JSON](#json)
    - [CSV (RFC 4180 Compliant)](#csv-rfc-4180-compliant)
    - [YAML (RFC-9512--yaml-122-compliant)](#yaml-rfc-9512--yaml-122-compliant)
    - [XML](#xml)
  - [Regular Expressions (POSIX ERE)](#regular-expressions-posix-ere)
  - [Async/Await & Promises](#asyncawait--promises)
  - [Network I/O](#network-io)
  - [File Operations](#file-operations)
  - [Date & Time](#date--time)
- [Directory Overview](#directory-overview)
- [Build Instructions](#build-instructions)
- [Using CupidScript in C Hosts](#using-cupidscript-in-c-hosts)
- [CupidFM Plugins](#cupidfm-plugins)
  - [Plugin Locations](#plugin-locations)
  - [Plugin Structure, Events & Lifecycle](#plugin-structure-events--lifecycle)
  - [Key Format and Helpers](#key-format-and-helpers)
- [fm.* API (Plugin Scripting API)](#fm-api-plugin-scripting-api)
  - [UI](#ui)
  - [Query Context](#query-context)
  - [Editor API](#editor-api)
    - [Editor State Functions](#editor-state-functions)
    - [Reading Editor Content](#reading-editor-content)
    - [Modifying Editor Content](#modifying-editor-content)
    - [Complete Editor Example](#complete-editor-example)
  - [Search Control](#search-control)
  - [Key Bindings & Events](#key-bindings--events)
  - [UI & System Control](#ui--system-control)
  - [File Navigation/Selection](#file-navigationselection)
  - [File Operations + Undo Integration](#file-operations--undo-integration)
  - [Minimal Example Plugin](#minimal-example-plugin)
  - [Example: API demo plugin](#example-api-demo-plugin)
- [Notes & Limitations](#notes--limitations)
- [CupidScript Standard Library & New Features](#cupidscript-standard-library--new-features)


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
- **Types/Values:** `nil`, `true`/`false`, int, float, string, bytes, list, map, set, tuple, strbuf, range, promise, function, native
- **Keywords:** `let`, `const`, `fn`, `async`, `await`, `yield`, `class`, `struct`, `enum`, `self`, `super`, `if`, `else`, `while`, `switch`, `case`, `default`, `for`, `in`, `break`, `continue`, `return`, `throw`, `try`, `catch`, `finally`, `defer`, `match`, `import`, `export`

### Modern Language Features

CupidScript includes cutting-edge features found in modern scripting languages:

- **Tuples** - Immutable, fixed-size value groupings
  ```cs
  let point = (x: 10, y: 20);  // named tuple
  let coords = (1, 2, 3);      // positional tuple
  print(point.x, coords[0]);
  ```

- **Comprehensions** - Concise syntax for transforming collections
  ```cs
  let squares = [x * x for x in 1..=10];
  let evens = [x for x in nums if x % 2 == 0];
  let word_map = {word: len(word) for word in words};
  let unique = #{x for x in list};  // set comprehension
  ```

- **Destructuring** - Extract values into variables
  ```cs
  let [a, b, ...rest] = [1, 2, 3, 4, 5];
  let (x, y) = get_coords();  // function returning tuple
  ```

- **Pattern Matching** - `match` expressions for powerful branching
  ```cs
  let result = match value {
    0 => "zero",
    1..=5 => "small",
    _ => "large"
  };
  ```

- **Walrus Operator** - Assign and test in one expression
  ```cs
  if (result := compute()) {
    print("Success:", result);
  }
  while (line := read_line()) {
    process(line);
  }
  ```

- **Pipe Operator** - Chain function calls fluently
  ```cs
  let result = data |> filter(_) |> map(_) |> sum();
  ```

- **Arrow Functions** - Concise function syntax
  ```cs
  let add = fn(a, b) => a + b;
  let square = fn(x) => x * x;
  ```

- **Spread & Rest** - Flexible argument handling
  ```cs
  let all = [...list1, ...list2];
  let merged = {...map1, ...map2};
  fn sum(...nums) { return list_sum(nums); }
  ```

- **Sets** - Unique collections with set operations
  ```cs
  let s = #{1, 2, 3};
  let union = set1 | set2;
  let intersection = set1 & set2;
  let difference = set1 - set2;
  ```

- **Classes & Inheritance** - Object-oriented programming
  ```cs
  class File {
    fn new(path) { self.path = path; }
    fn is_hidden() { return starts_with(self.path, "."); }
  }
  class ImageFile : File {
    fn is_image() { return ends_with(self.path, ".png"); }
  }
  ```

- **Structs** - Lightweight data carriers
  ```cs
  struct Point { x, y = 0 }
  let p = Point(5, 10);
  ```

- **Enums** - Named integer constants
  ```cs
  enum Color { Red, Green = 5, Blue }
  print(Color.Red);   // 0
  ```

- **Async/Await** - Asynchronous programming
  ```cs
  async fn fetch_data(url) {
    let response = await http_get(url);
    return response;
  }
  ```

- **Generators** - Lazy value sequences
  ```cs
  fn range(n) {
    let i = 0;
    while (i < n) { yield i; i += 1; }
  }
  ```

- **String Interpolation** - Embed expressions in strings
  ```cs
  let name = "Alice";
  print("Hello ${name}, you have ${count} messages");
  ```

- **Raw Strings** - Backtick strings without escape processing
  ```cs
  let path = `C:\Users\Frank\Documents`;
  let multiline = `line 1
  line 2`;
  ```

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
- **Defer statements** - Execute code when leaving scope
  ```cs
  fn process() {
    let f = open_file("data.txt");
    defer close_file(f);  // always called before returning
    // ... work with file ...
  }
  ```
- **Const bindings** - Immutable variable declarations
  ```cs
  const PI = 3.14159;
  const MAX_SIZE = 100;
  ```
- **`typeof` returns detailed type names ("native", "function", "tuple", "set", etc)**
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

## Built-In Types

CupidScript supports a rich set of built-in types:

### Core Types

```cs
let xs = [1, 2, 3];              // list
let m = {"name": "Frank"};       // map
let s = #{1, 2, 3};              // set
let t = (x: 10, y: 20);          // tuple (named)
let coords = (1, 2, 3);          // tuple (positional)
let b = bytes([0x48, 0x65]);     // bytes
let r = 1..10;                   // range
```

### Lists

```cs
let xs = list();
push(xs, 10);
push(xs, 20);
xs[1] = 99;
print(xs[0], xs[1], len(xs)); // 10 99 2
```
- Index by integer (0-based)
- Negative or out-of-range returns `nil`
- Dynamic and mutable
- Supports spread: `[...list1, ...list2]`

### Maps

```cs
let m = map();
m["answer"] = 42;
print(m["answer"], keys(m)); // 42 ["answer"]
print(m.answer);  // field access sugar
```
- **Generalized keys**: Any value can be a key (string, int, bool, list, map, etc.)
- Key equality uses `==` rules (int/float compare by value, strings by content)
- Missing keys return `nil`
- Supports spread: `{...map1, ...map2}`

### Sets

```cs
let s = #{1, 2, 3};
s.add(4);
s.remove(2);
print(s.contains(3));  // true
print(s.size());       // 3

// Set operations
let union = set1 | set2;
let intersection = set1 & set2;
let difference = set1 - set2;
let symmetric_diff = set1 ^ set2;
```
- Unique values (by `==`)
- Set operations: `|` (union), `&` (intersection), `-` (difference), `^` (symmetric difference)
- Literal: `#{1, 2, 3}` or `#{}` for empty
- Comprehensions: `#{x for x in list if condition}`

### Tuples

```cs
// Positional tuple
let coords = (10, 20, 30);
print(coords[0], coords[1], coords[2]);

// Named tuple
let point = (x: 10, y: 20);
print(point.x, point.y);

// Destructuring
let (a, b, c) = coords;
let [x, y] = get_position();
```
- **Immutable** - cannot modify after creation
- Access by index (positional) or field name (named)
- Perfect for returning multiple values from functions

### Bytes

```cs
let b = bytes([72, 101, 108, 108, 111]);  // "Hello"
print(b[0]);  // 72
b[0] = 104;   // modify to "hello"
```
- Mutable byte buffer for binary data
- Index returns int 0-255
- Out-of-range returns `nil`

### Range

```cs
let r = 0..5;      // [0,1,2,3,4] - exclusive end
let incl = 0..=5;  // [0,1,2,3,4,5] - inclusive end
for i in 1..=10 { print(i); }
```
- Works in both directions (ascending/descending)
- Can iterate with `for ... in`

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

## Advanced Features

### Data Formats

CupidScript provides built-in support for structured data formats:

#### JSON
```cs
let data = {"name": "Frank", "age": 30};
let json_str = json_encode(data);
let parsed = json_decode(json_str);
```

#### CSV (RFC 4180 Compliant)
```cs
let csv_data = csv_parse("name,age\nFrank,30\nAlice,25");
let csv_str = csv_format([{"name": "Frank", "age": 30}]);
```

#### YAML (RFC 9512 / YAML 1.2.2 Compliant)
```cs
let yaml_str = "name: Frank\nage: 30";
let data = yaml_parse(yaml_str);
let yaml = yaml_format(data);
```

#### XML
```cs
let xml = "<root><item>test</item></root>";
let doc = xml_parse(xml);
```

### Regular Expressions (POSIX ERE)

```cs
// Match checking
if (regex_is_match("[0-9]+", text)) {
    print("Contains digits");
}

// Find first match
let email = regex_find("([a-z]+)@([a-z]+\\.[a-z]+)", text);
if (email != nil) {
    print(email["match"]);     // full match
    print(email.groups[0]);    // first capture group
}

// Find all matches
let nums = regex_find_all("[0-9]+", "x=7 y=42 z=105");

// Replace
let clean = regex_replace("[a-z]+@[a-z]+\\.[a-z]+", text, "<hidden>");
```

### Async/Await & Promises

```cs
// Event loop (background thread for true async)
event_loop_start();

// Async functions
async fn fetch_data(url) {
    let response = await http_get(url);
    return response;
}

// Multiple concurrent operations
let p1 = fetch_data("https://api.example.com/1");
let p2 = fetch_data("https://api.example.com/2");
let result1 = await p1;
let result2 = await p2;

// Sleep/timers
await sleep(1000);  // milliseconds

event_loop_stop();
```

**Promise Helpers:**
- `promise()` - create new promise
- `resolve(p, value)` - resolve promise
- `reject(p, error)` - reject promise
- `promise_all(promises)` - wait for all
- `promise_race(promises)` - wait for first
- `promise_any(promises)` - wait for first success

### Network I/O

```cs
// HTTP requests (async)
let response = await http_get("https://api.github.com/users/octocat");
let data = json_decode(response);

// TCP sockets
let sock = await tcp_connect("example.com", 80);
await socket_send(sock, "GET / HTTP/1.1\r\nHost: example.com\r\n\r\n");
let response = await socket_recv(sock, 4096);
socket_close(sock);
```

### File Operations

```cs
// Read/write files
let content = read_file("config.txt");
write_file("output.txt", "Hello, world!");

// Binary files
let data = read_file_bytes("image.png");
write_file_bytes("copy.png", data);

// Directory operations
let entries = list_dir(".");
mkdir("new_folder");
rm("old_file.txt");
rename("old.txt", "new.txt");

// File info
if (exists("file.txt") && is_file("file.txt")) {
    print("File exists");
}
if (is_dir("folder")) {
    print("Directory exists");
}

// Glob patterns (platform-specific)
let txt_files = glob("*.txt");
let all_cs = glob("**/*.cs", "src");
```

### Date & Time

```cs
// Current time
let ms = unix_ms();
let seconds = unix_s();

// Date/time maps
let now = datetime_now();
let utc = datetime_utc();
print("${now.year}-${now.month}-${now.day} ${now.hour}:${now.minute}");

// Convert from Unix timestamp
let dt = datetime_from_unix_ms(ms);
```

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

### Editor API

CupidFM provides a comprehensive API for manipulating the built-in text editor from plugins. All editor functions use **1-indexed** line and column numbers.

#### Editor State Functions

| Function | Returns | Description |
|----------|---------|-------------|
| `fm.editor_active()` | `bool` | True if the built-in text editor is currently open |
| `fm.editor_get_path()` | `string \| nil` | Current editor file path, or nil if editor not open |
| `fm.editor_line_count()` | `int` | Total number of lines in the editor, or 0 if not open |
| `fm.editor_get_cursor()` | `map \| nil` | Cursor position `{line: int, col: int}` (1-indexed), or nil if not editing |
| `fm.editor_set_cursor(line, col)` | `bool` | Sets cursor position (1-indexed). Returns true on success, false if not editing or invalid position |
| `fm.editor_get_selection()` | `map \| nil` | Selection bounds `{start_line, start_col, end_line, end_col}` (1-indexed), or nil if no selection |

#### Reading Editor Content

| Function | Returns | Description |
|----------|---------|-------------|
| `fm.editor_get_content()` | `string \| nil` | Entire editor buffer text, or nil if editor not open |
| `fm.editor_get_line(line_num)` | `string \| nil` | Single line content (1-indexed), or nil if out of range |
| `fm.editor_get_lines(start, end)` | `list \| nil` | List of lines in range (1-indexed, inclusive), or nil if invalid |

**Example:**
```cs
if (fm.editor_active()) {
    let path = fm.editor_get_path();
    let line_count = fm.editor_line_count();
    let cursor = fm.editor_get_cursor();
    
    fm.notify(fmt("Editing: %s (%d lines) at line %d", path, line_count, cursor["line"]));
    
    // Get first 10 lines
    let lines = fm.editor_get_lines(1, 10);
    for line in lines {
        print(line);
    }
    
    // Move cursor to line 5, column 10
    if (fm.editor_set_cursor(5, 10)) {
        fm.notify("Cursor moved to line 5, column 10");
    }
}
```

#### Modifying Editor Content

| Function | Parameters | Returns | Description |
|----------|-----------|---------|-------------|
| `fm.editor_insert_text(text)` | `text: string` | `bool` | Inserts text at current cursor position |
| `fm.editor_replace_text(...)` | `start_line, start_col, end_line, end_col, text` | `bool` | Replaces text in specified range with new text |
| `fm.editor_delete_range(...)` | `start_line, start_col, end_line, end_col` | `bool` | Deletes text in specified range |
| `fm.editor_uppercase_selection()` | - | `bool` | Converts current selection to uppercase (efficient built-in) |

**fm.editor_replace_text(start_line, start_col, end_line, end_col, text)**
- All coordinates are 1-indexed
- `text` can include newlines for multi-line replacements
- Example: `fm.editor_replace_text(1, 1, 1, 10, "new text")` replaces characters 1-10 on line 1
- See [`plugins/examples/editor_find_replace_demo.cs`](plugins/examples/editor_find_replace_demo.cs) for complete find-and-replace example

**fm.editor_delete_range(start_line, start_col, end_line, end_col)**
- All coordinates are 1-indexed
- Deletes text from `(start_line, start_col)` to `(end_line, end_col)`
- Example: `fm.editor_delete_range(1, 1, 1, 10)` deletes characters 1-10 on line 1
- See [`plugins/examples/editor_delete_operations_demo.cs`](plugins/examples/editor_delete_operations_demo.cs) for examples including word deletion, line deletion, etc.

**fm.editor_uppercase_selection()**
- Only works when editor is active and text is selected
- More efficient than manually getting/replacing text for case conversion
- See [`plugins/examples/editor_text_manipulation_demo.cs`](plugins/examples/editor_text_manipulation_demo.cs) for usage with Ctrl+U keybinding

#### Complete Editor Example

```cs
fn on_load() {
    fm.bind("^F", "find_and_replace");
}

fn find_and_replace(key) {
    if (!fm.editor_active()) {
        fm.notify("Editor not active");
        return true;
    }
    
    let sel = fm.editor_get_selection();
    if (sel == nil) {
        fm.notify("No selection");
        return true;
    }
    
    let find = fm.prompt("Find:", "");
    if (find == nil || find == "") return true;
    
    let replace = fm.prompt("Replace with:", "");
    if (replace == nil) return true;
    
    // Get selected text
    let start_line = sel["start_line"];
    let start_col = sel["start_col"];
    let end_line = sel["end_line"];
    let end_col = sel["end_col"];
    
    let lines = fm.editor_get_lines(start_line, end_line);
    // ... process and replace text ...
    
    return true;
}
```

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
