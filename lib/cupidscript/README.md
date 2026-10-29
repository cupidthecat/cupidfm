# CupidScript

A lightweight C99 VM for a small scripting language. CupidScript is a modern, feature-rich scripting language designed for embedding in C applications. It combines practical features like classes, async/await, pattern matching, and comprehensions with a clean C99 implementation that's easy to embed and extend.

## Design Philosophy

**Practical Embedding First**: CupidScript is designed to be embedded in C applications with minimal friction. The entire VM is written in portable C99 with a clean API, making it easy to integrate into existing projects.

**Modern Features, Simple Implementation**: While CupidScript includes modern language features (classes, async/await, generators, pattern matching, comprehensions), the implementation remains straightforward—a tree-walking interpreter without complex JIT compilation or bytecode.

**Safety Without Compromise**: Built-in instruction limits and timeout controls protect host applications from runaway scripts. Scripts can't silently create variables through typos (assignment requires prior declaration with `let`).

**Batteries Included**: The standard library provides comprehensive functionality for real-world scripting: JSON, regex, filesystems, CSV, XML, HTTP, subprocess management, and more, all accessible without external dependencies (except OpenSSL for HTTPS).

**Interoperable**: First-class support for data interchange (JSON, YAML), external processes, and C API integration means CupidScript works well as glue code and configuration language.

---

## Documentation & Wiki

See the [Wiki Home](https://github.com/frankischilling/cupidscript/wiki) for detailed documentation, guides, and language reference.

---

## What’s Included

- **Core runtime:** lexer, parser, AST, VM, and a small standard library.
- **Sample CLI (`src/main.c`):** demonstrates embedding and registering `fm.*` natives.
- **Public headers:** `src/cupidscript.h` is the embedder-facing API.
- **Examples/tests:** small scripts that exercise language features and stdlib.

---

## Feature Highlights

**Modern Syntax**
- **Walrus Operator**: `if ((result := compute()) > 10) { use(result); }` - assign and use in same expression
- List/Map Comprehensions: `[x*2 for x in nums if x > 5]`, `{k: v for k, v in map}`
- Pattern Matching: `match value { 0 => "zero", n if n > 10 => "big", _ => "other" }`
- Pipe Operator: `data |> filter(fn) |> map(transform) |> reduce(sum)`
- Optional Chaining: `user?.profile?.name ?? "Guest"`
- String Interpolation: `"Hello ${name}, you are ${age} years old"`
- Destructuring: `let [a, b, ...rest] = list; let {x, y} = point;`
- Spread/Rest: `[0, ...xs, 10]`, `fn sum(...nums) { }`
- Arrow Functions: `fn(x) => x * 2`
- Raw Strings: `r"C:\path\to\file"` (no escape processing)

**Object-Oriented Programming**
- Classes with single inheritance and `super` calls
- Structs with positional/named field construction
- Enums as named integer constants
- Tuples (immutable): `(10, 20)` or `(x: 10, y: 20)`

**Functional Programming**
- First-class functions and closures
- Generators with `yield`
- Higher-order functions: `filter`, `map_list`, `reduce`, `zip`
- Tail recursion optimization (for simple recursive patterns)

**Async & Error Handling**
- `async`/`await` syntax (synchronous execution currently)
- Structured error objects with stack traces
- `try`/`catch`/`finally` blocks
- Optional chaining for nil-safe access

**Standard Library**
- **Data**: JSON, YAML 1.2.2, CSV, XML parsing/generation
- **Collections**: Comprehensive list/map/string helpers
- **Files**: Filesystem operations, glob patterns, file watching
- **Network**: HTTP client (with TLS via OpenSSL)
- **Regex**: Full regex matching and replacement
- **Process**: Subprocess execution and management
- **Archives**: TAR and ZIP creation/extraction
- **Safety**: Instruction limits, timeouts, memory controls

---

## Quick Start

```sh
make
bin/cupidscript examples/features.cs
```

## Dependencies

TLS/HTTPS support requires OpenSSL development libraries.

- **Linux (Debian/Ubuntu):** `libssl-dev`
- **Linux (Fedora/RHEL):** `openssl-devel`

To build without TLS, set `CS_NO_TLS=1`:

```sh
make CS_NO_TLS=1
```

## Tests

Tests are plain `.cs` scripts under `tests/` that use the stdlib `assert(...)` function.

```sh
make test
```

### Writing New Tests

1. **Create a new test file** in `tests/` with a `.cs` extension (e.g. `tests/my_feature.cs`).
2. **Write assertions** with `assert(condition, "message")` so failures are clear.
3. **Print a success marker** at the end (optional, but helps when reading logs).

Example:

```cs
// tests/my_feature.cs
let x = 1 + 2;
assert(x == 3, "basic math");
print("my_feature ok");
```

#### Negative Tests (Expected Failures)

If a test should *fail* (e.g. parse/runtime errors), add this header at the top:

```cs
// EXPECT_FAIL
```

Example:

```cs
// tests/negative_example.cs
// EXPECT_FAIL
assert(false, "should fail");
```

#### Helper Files

Files prefixed with `_` are ignored by the test runner. Use these for shared helpers or fixtures.

Other useful scripts:
- `bin/cupidscript examples/test.cs`
- `bin/cupidscript examples/stress.cs`
- `bin/cupidscript examples/stacktrace.cs`
- `bin/cupidscript examples/trycatch.cs`
- `bin/cupidscript examples/try_finally.cs`
- `bin/cupidscript examples/throwtrace.cs`
- `bin/cupidscript examples/closures.cs`
- `bin/cupidscript examples/time.cs`
- `bin/cupidscript examples/benchmark.cs`
- `bin/cupidscript examples/gc_cycle.cs`
- `bin/cupidscript examples/gc_auto.cs`
- `bin/cupidscript examples/safety_config.cs`
- `bin/cupidscript examples/safety_test.cs`
- `bin/cupidscript examples/filesystem.cs`
- `bin/cupidscript examples/json.cs`
- `bin/cupidscript examples/json_example.cs`
- `bin/cupidscript examples/list_helpers.cs`
- `bin/cupidscript examples/arrow_functions.cs`
- `bin/cupidscript examples/spread_rest.cs`
- `bin/cupidscript examples/pipe_operator.cs`
- `bin/cupidscript examples/classes.cs`
- `bin/cupidscript examples/default_params.cs`
- `bin/cupidscript examples/match_expr.cs`
- `bin/cupidscript examples/match_patterns.cs`
- `bin/cupidscript examples/destructuring.cs`
- `bin/cupidscript examples/structs.cs`
- `bin/cupidscript examples/enums.cs`
- `bin/cupidscript examples/generators.cs`

---

## Language Overview

CupidScript is intentionally small: a tree-walk interpreter with dynamic values and a C embedding API. It includes modern features while maintaining simplicity.

### Core Features

- **Variables & Functions**: `let` declarations, function literals, arrow functions `=>`, closures, default parameters
- **Classes & Inheritance**: First-class classes with `self` binding, single inheritance with `super`
- **Structs & Enums**: Fixed-field structs with positional construction, enums as named integer constants
- **Pattern Matching**: Powerful `match` expressions with destructuring, guards, and type patterns
- **Comprehensions**: List and map comprehensions with filtering: `[x*2 for x in nums if x > 5]`
- **Tuples**: Immutable ordered collections with named/positional fields
- **Async/Await**: `async` functions and `await` expressions (currently synchronous)
- **Generators**: Functions using `yield` to produce sequences
- **Error Handling**: `try`/`catch`/`finally`, structured error objects
- **Module System**: `require()` for modules with `export` maps, `load()` for script execution
- **Modern Operators**: Pipe `|>`, spread/rest `...`, optional chaining `?.`, nullish coalescing `??`, ranges `..` and `..=`

### Syntax (core)

```cs
let name = expr;     // declaration (optional initializer)
name = expr;         // assignment (must already exist)
name += expr;        // compound assignment (also -=, *=, /=)

fn add(a, b) {       // function definition
  return a + b;
}

fn add2(a, b) => a + b;       // arrow function
fn greet(name, greeting = "Hello") {
  return greeting + ", " + name + "!";
}

async fn add(a, b) { return a + b; }
let sum = await add(2, 3);

if (cond) { ... } else { ... }
while (cond) { ... }
for x in expr { ... }           // for-in over lists/maps
for (init; cond; incr) { ... }  // C-style for loop
break;
continue;
return expr;

throw expr;
try { ... } catch (e) { ... } finally { ... }
export name = expr; // module exports
switch (expr) { case 1 { ... } default { ... } }

// structs + enums
struct Point { x, y = 0 }
enum Color { Red, Green = 5, Blue }

// tuples
let point = (10, 20);           // positional tuple
let named = (x: 10, y: 20);     // named tuple
print(point[0], named.x);       // 10 10

// generators
fn range(n) {
  let i = 0;
  while (i < n) { yield i; i += 1; }
}

// classes
class File {
  fn new(path) { self.path = path; }
  fn is_hidden() { return starts_with(self.path, "."); }
}

class ImageFile : File {
  fn new(path) { super.new(path); self.is_img = ends_with(path, ".png"); }
  fn is_image() { return self.is_img; }
}

// pattern matching
let result = match value {
  0 => "zero",
  1 | 2 => "one or two",
  n if n > 10 => "big",
  _ => "other"
};

// comprehensions
let evens = [x for x in range(10) if x % 2 == 0];
let squares = {x: x*x for x in nums};
let items = {k: v for k, v in map if v != nil};
```

Assignment rule (intentional): `let` creates new variables; plain assignment (`x = ...`) errors if `x` was never declared. This prevents silent typos like `coutn = 1`.

### Expressions

- Operators with precedence: `||`, `&&`, `==`, `!=`, `<`, `<=`, `>`, `>=`, `+`, `-`, `*`, `/`, `%`, unary `!`, unary `-`
- **Walrus operator**: `:=` assigns and returns a value in one expression: `if ((x := compute()) > 10) { use(x); }`
- Nullish coalescing: `??` returns right operand if left is `nil`
- Pipe operator: `|>` passes left value into right call, supports `_` placeholder
- Range operators: `start..end` (exclusive), `start..=end` (inclusive)
- Ternary: `cond ? a : b`
- Optional chaining: `obj?.field` returns `nil` if `obj` is `nil`
- Strings: `"..."` with escapes `\n`, `\t`, `\r`, `\"`, `\\`
- Raw strings: `r"..."` with no escape processing
- String interpolation: `"value is ${x + 1}"`
- Integers: decimal (`123`), hex (`0xFF`), underscores (`1_000_000`)
- Floats: decimal with dot (`3.14`), scientific notation (`1.5e-3`, `2e10`)
- Literals: list `[1, 2, "hi"]`, map `{ "a": 1, "b": 2 }`, tuple `(1, 2)` or `(x: 1, y: 2)`
- Spread in literals: `[0, ...xs]`, `{...m1, ...m2}`
- Comprehensions: `[expr for var in iter if cond]`, `{k: v for var in iter if cond}`
- Dynamic values: `nil`, `true/false`, integers, floats, strings, functions, native functions, lists, maps, strbuf, tuples
- Structs: fixed-field maps with positional construction
- Enums: named integer constants stored in a map
- Generators: functions that use `yield` and return a list of yielded values
- Async/Await: `async` functions and `await` (currently synchronous execution)

Notes:
- `+` supports `int + int`, `float + float`, mixed arithmetic (int+float→float), and string concatenation (if either operand is a string).
- `/` division always returns float for precision
- `&&` / `||` short-circuit.

### Lists, Maps, and Comprehensions

Lists and maps are first-class dynamic values. Comprehensions provide concise syntax for creating collections.

```cs
let xs = [10, 20];
let ys = [0, ...xs, 30];
xs[1] = 99;
print(xs[0], xs[1], len(xs)); // 10 99 2

let m = { "name": "cupid" };
let m2 = {"name": "cupid", "version": 1};
let m3 = {...m, ...m2};
print(m["name"]);             // cupid
print(keys(m));               // list of keys

// List comprehensions
let evens = [x for x in range(20) if x % 2 == 0];
let doubled = [x * 2 for x in [1, 2, 3]];

// Map comprehensions
let squares = {x: x*x for x in range(5)};
let filtered = {k: v for k, v in map if v != nil};

// Iterate over strings
let chars = [c for c in "hello"];  // ["h", "e", "l", "l", "o"]
```
let xs = [10, 20];
let ys = [0, ...xs, 30];
xs[1] = 99;
print(xs[0], xs[1], len(xs)); // 10 99 2

let m = { "name": "cupid" };
let m2 = {"name": "cupid", "version": 1};
let m3 = {...m, ...m2};
print(m["name"]);             // cupid
print(keys(m));               // list of keys
```

List helpers:

```cs
extend(xs, ys);                // append elements
index_of(xs, 42);              // index or -1
sort(xs);                      // insertion (default)
sort(xs, "quick");            // quicksort
sort(xs, "merge");            // mergesort
filter(xs, fn(x) => x > 5);   // keep matching elements
map_list(xs, fn(x) => x*2);   // transform elements
reduce(xs, fn(acc, x) => acc + x, 0);  // fold
```

Indexing rules:
- `list[int]` -> element or `nil` if out of range
- `map[string]` -> value or `nil` if missing
- `tuple[int]` -> element by position
- `tuple.field` -> element by name (named tuples)
- Assignment supports `xs[i] = v` and `m["k"] = v`

Practical plugin pattern (structured state/config):

```cs
let state = map();
state["enabled"] = true;
state["bindings"] = list();
push(state["bindings"], "F5");
```

Map keys can be any value; equality follows the same rules as `==` (so `1` and `1.0` collide).

### Multi-file scripts

Stdlib provides:
- `load("file.cs")`: executes another script file every time it’s called
- `require("file.cs")`: executes a file once per VM and returns its `exports` map
- `require_optional("file.cs")`: like `require`, but returns `nil` if file doesn't exist (no error)
- `cwd()` / `chdir(path)` to inspect or change the VM's current directory

Paths are resolved relative to the currently-running script file’s directory.

Module pattern:

```cs
// lib.cs
export hello = fn(name) { return "hello " + name; };

// main.cs
let lib = require("lib.cs");
print(lib.hello("world"));
```

### Field access and method calls

`obj.field` is supported as field access:
- If `obj` is a map, `obj.field` returns the same value as `obj["field"]`.

`obj.method(a, b)` is supported as a method call:
- If `obj` is a map, `obj.method(...)` calls the function stored at `obj["method"]` (no implicit `self` argument).
- `strbuf` also exposes methods like `b.append(...)`, `b.str()`, `b.len()`, `b.clear()`.

Compatibility note: CupidFM-style dotted globals still work (e.g. `fm.status("hi")`) even if `fm` is not a script value; the VM falls back to looking up a global named `"fm.status"`.

### Classes and `self` / `super`

Classes are first-class values and are called to create instances. Methods are invoked on instances, with `self` bound automatically.

```cs
class File {
  fn new(path) { self.path = path; }
  fn is_hidden() { return starts_with(self.path, "."); }
}

class ImageFile : File {
  fn new(path) { super.new(path); self.is_img = ends_with(path, ".png"); }
  fn is_image() { return self.is_img; }
}

let img = ImageFile("cat.png");
print(img.is_image());
```

### Rest parameters

Functions can collect extra arguments into a list:

```cs
fn log_all(prefix, ...items) {
  for item in items { print(prefix, item); }
}
```

### Pipe operator

```cs
fn add(a, b) { return a + b; }
print(10 |> add(5));     // add(10, 5)
print(10 |> add(5, _));  // add(5, 10)
```

---

## Standard Library Highlights

**Collections:**
- `list`, `map`, `len`, `push`, `pop`, `extend`, `index_of`, `insert`, `remove`, `slice`, `keys`, `values`, `items`, `map_values`
- `reverse`, `reversed`, `contains`, `copy`, `deepcopy`, `sort(list, [cmp], [algo])`
- `filter`, `map_list`, `reduce`, `zip`, `enumerate`, `flatten`
- Comprehensions: `[expr for var in iter if cond]`, `{k: v for var in iter if cond}`

**Strings:**
- `str_find`, `str_replace`, `str_split`, `substr`, `join`, `to_str`, `to_int`, `to_float`
- `trim/ltrim/rtrim`, `lower/upper`, `starts_with/ends_with`, `str_repeat`, `split_lines`
- String interpolation: `"result: ${x + 1}"`
- Raw strings: `r"\path\to\file"` (no escape processing)

**Data Formats:**
- **JSON**: `json_parse(text)`, `json_stringify(value)`
- **YAML**: `yaml_parse(text)`, `yaml_stringify(value)` (YAML 1.2.2 compliant)
- **CSV**: `csv_parse(text)`, `csv_stringify(rows)`
- **XML**: `xml_parse(text)` returns document tree

**Filesystem & Paths:**
- `read_file`, `write_file`, `exists`, `is_dir`, `is_file`, `list_dir`, `mkdir`, `rm`, `rename`
- `path_join`, `path_dirname`, `path_basename`, `path_ext`, `cwd`, `chdir`
- `glob(pattern)` for file pattern matching
- `file_watch(path, callback)` for filesystem monitoring

**Network & I/O:**
- `http_get`, `http_post`, `http_request` with full header/body control
- `subprocess_run`, `subprocess_spawn` for external process execution
- Archive support: `create_tar`, `extract_tar`, `create_zip`, `extract_zip`

**Regex:**
- `regex_match(pattern, text)`, `regex_find_all(pattern, text)`, `regex_replace(pattern, text, replacement)`

**Time & Safety:**
- `now_ms`, `sleep`, `date_time` for parsing/formatting timestamps
- `set_timeout`, `set_instruction_limit`, `get_timeout`, `get_instruction_limit`, `get_instruction_count`

**Errors:**
- `error`, `is_error`, `format_error`, `ERR` map

### Function references and closures

Functions are values, so you can pass them to native APIs as callbacks:

```cs
fn on_key(key) {
  print("key:", key);
}

// Example: fm.on("key", on_key)
```

CupidScript also supports closures (functions capturing variables) and anonymous function literals:

```cs
fn make_counter() {
  let n = 0;
  return fn() { n = n + 1; return n; };
}

let c = make_counter();
print(c(), c(), c()); // 1 2 3
```

---

## Directory Overview

- `src/cs_value.c`, `src/cs_lexer.c`, `src/cs_parser.c`, `src/cs_vm.c`, `src/cs_stdlib.c` – core runtime and standard library implementations.
- `src/main.c` – sample program showing how to bootstrap the VM and expose native functions (the `fm.*` API in this project).
- **Headers:** `src/cupidscript.h`, `src/cs_vm.h`, `src/cs_value.h` – public API and value types.
- `Makefile` – simple build system producing a library and a small executable.

---

## Build

### Prerequisites

- A C99-compliant compiler (gcc/clang) and a POSIX-like toolchain.
- `make` (as specified by the provided Makefile).

### Build with Make (recommended)

```sh
make all
```

This should produce:
- `libcupidscript.a` in `bin/`
- `cupidscript` (executable) in `bin/`

### Manual Build (if you don’t have make)

```sh
CC=gcc
CFLAGS="-std=c99 -Wall -Wextra -O2 -g -D_POSIX_C_SOURCE=200809L"
SRCDIR=src
OBJDIR=obj
BINDIR=bin
AR=ar
ARFLAGS=rcs

mkdir -p $OBJDIR $BINDIR

# Compile sources
for f in cs_value.c cs_lexer.c cs_parser.c cs_vm.c cs_stdlib.c; do
  $CC $CFLAGS -Isrc -c "$SRCDIR/$f" -o "$OBJDIR/${f%.*}.o"
done

# Create library
$AR $ARFLAGS "$BINDIR/libcupidscript.a" "$OBJDIR"/*.o

# Compile CLI (example main)
$CC $CFLAGS -Isrc -c "$SRCDIR/main.c" -o "$OBJDIR/main.o"
# Link executable
$CC $CFLAGS -Isrc "$OBJDIR/main.o" "$BINDIR/libcupidscript.a" -o "$BINDIR/cupidscript"
```

---

## Usage

#### Embedder usage (typical flow):

1. Create a VM:
   ```c
   cs_vm* vm = cs_vm_new();
   ```
2. Register stdlib:
   ```c
   cs_register_stdlib(vm);
   ```
3. Expose natives:
   ```c
   cs_register_native(vm, "my.native", my_fn, NULL);
   ```
4. Run code from file:
   ```c
   cs_vm_run_file(vm, "script.cs");
   ```
   Or run from a string:
   ```c
   cs_vm_run_string(vm, code, "<string>");
   ```
5. Call a named script function:
   ```c
   cs_call(vm, "function_name", argc, argv, &out);
   ```
   Or call a function value (callback) you previously stored:
   ```c
   cs_call_value(vm, fn_value, argc, argv, &out);
   ```
6. Retrieve errors:
   ```c
   const char* err = cs_vm_last_error(vm);
   ```
7. Strings:
   ```c
   cs_str(vm, "hello"); /* and convert to C with cs_to_cstr(out_val); */
   ```
8. **Safety controls** (prevent runaway scripts):
   ```c
   cs_vm_set_instruction_limit(vm, 10000000);  // max 10M instructions
   cs_vm_set_timeout(vm, 5000);                 // max 5000ms execution
   cs_vm_interrupt(vm);                         // interrupt from another thread
   ```
9. **GC auto-collect** (optional automatic garbage collection):
   ```c
   cs_vm_set_gc_threshold(vm, 1000);      // collect when tracked >= 1000
   cs_vm_set_gc_alloc_trigger(vm, 100);   // collect every 100 allocations
   ```

---

## Safety Controls

CupidScript includes built-in protection against runaway scripts that could hang the host application (CupidFM):

### Instruction Limit

Limits the total number of VM operations (expressions evaluated) per script execution:

```c
cs_vm_set_instruction_limit(vm, 10000000);  // 10 million instructions
```

- Default: `0` (unlimited)
- When exceeded, script aborts with error: `"instruction limit exceeded (N instructions)"`
- Counter resets at the start of each `cs_vm_run_file` / `cs_vm_run_string` call
- Get current count: `uint64_t count = cs_vm_get_instruction_count(vm);`

### Timeout

Limits the wall-clock execution time per script run:

```c
cs_vm_set_timeout(vm, 5000);  // 5 second timeout
```

- Default: `0` (unlimited)
- Time in milliseconds
- When exceeded, script aborts with error: `"execution timeout exceeded (N ms)"`
- Timer resets at the start of each script execution
- Checked every 1000 instructions to minimize overhead

### Interrupt

Allows the host to request immediate termination of a running script (useful for UI cancel buttons):

```c
cs_vm_interrupt(vm);  // Thread-safe signal
```

- Can be called from any thread while script is running
- Script aborts with error: `"execution interrupted by host"`
- Flag is automatically reset at the start of each script execution

### Example Usage

```c
cs_vm* vm = cs_vm_new();
cs_register_stdlib(vm);

// Protect against infinite loops in user plugins
cs_vm_set_instruction_limit(vm, 50000000);  // 50M instructions
cs_vm_set_timeout(vm, 10000);                // 10 second timeout

int rc = cs_vm_run_file(vm, "user_plugin.cs");
if (rc != 0) {
    fprintf(stderr, "Plugin error: %s\n", cs_vm_last_error(vm));
    fprintf(stderr, "Instructions executed: %llu\n",
            (unsigned long long)cs_vm_get_instruction_count(vm));
}
```

See `examples/safety_demo.c` for a complete demonstration.

### Script-Level Safety Controls

Scripts can also query and configure their own safety limits:

```c
// Get current limits
let timeout = get_timeout();                 // milliseconds
let limit = get_instruction_limit();         // instruction count
let count = get_instruction_count();         // current count

// Set stricter limits for a critical section
set_timeout(1000);                           // 1 second
set_instruction_limit(10000000);             // 10M instructions

// Heavy processing...
```

See `examples/safety_config.cs` and `examples/safety_test.cs` for demonstrations.

---

## C API for List and Map Manipulation

The C API provides retained values for lists, maps, and tuples. See the [embedding guide](wiki/Embedding-API.md) for ownership rules and a complete host example.

### List Operations

```c
cs_value mylist = cs_list(vm);
cs_list_push(mylist, cs_int(42));
cs_value greeting = cs_str(vm, "hello");
cs_list_push(mylist, greeting);
cs_value_release(greeting);

size_t len = cs_list_len(mylist);
cs_value val = cs_list_get(mylist, 0);
cs_list_set(mylist, 1, cs_float(3.14));
cs_value last = cs_list_pop(mylist);
cs_value_release(val);
cs_value_release(last);
cs_value_release(mylist);
```

### Map Operations

```c
cs_value mymap = cs_map(vm);
cs_value label = cs_str(vm, "Alice");
cs_map_set(mymap, "name", label);
cs_value_release(label);
cs_map_set(mymap, "age", cs_int(30));

if (cs_map_has(mymap, "name")) {
    cs_value name = cs_map_get(mymap, "name");
    printf("Name: %s\n", cs_to_cstr(name));
    cs_value_release(name);
}

cs_value keys = cs_map_keys(vm, mymap);
cs_map_del(mymap, "age");
cs_value_release(keys);
cs_value_release(mymap);
```

All returned `cs_value` objects must be released with `cs_value_release()`.

---

## Key Types and API

- **Value types** (`cs_type`, exposed via `cupidscript.h`):
  - `CS_T_NIL`, `CS_T_BOOL`, `CS_T_INT`, `CS_T_FLOAT`, `CS_T_STR`, `CS_T_LIST`, `CS_T_MAP`, `CS_T_STRBUF`, `CS_T_FUNC`, `CS_T_NATIVE`
- **Core value wrapper:**
  ```c
  cs_value { type; union { int b; int64_t i; double f; void* p; } as; }
  ```
- **Public helpers:**
  - `cs_vm_new`, `cs_vm_free`
  - `cs_vm_run_file`, `cs_vm_run_string`
  - `cs_vm_collect_cycles` (collect list/map cycles)
  - `cs_vm_set_gc_threshold`, `cs_vm_set_gc_alloc_trigger` (GC auto-collect)
  - `cs_vm_set_instruction_limit`, `cs_vm_set_timeout`, `cs_vm_interrupt`, `cs_vm_get_instruction_count` (safety controls)
  - `cs_register_native`, `cs_call`
  - `cs_call_value` (call a function value from C)
  - `cs_vm_last_error` / `cs_error` (get/set VM error from native code)
  - `cs_last_error` (compat getter; returns `NULL` if no error)
  - `cs_to_cstr`, `cs_nil`, `cs_bool`, `cs_int`, `cs_str`, `cs_str_take`
  - `cs_list`, `cs_map`, `cs_strbuf`
  - `cs_value_copy`, `cs_value_release` (retain/release values for host storage)

---

## Standard Library (Current)

CupidScript’s stdlib is implemented in `src/cs_stdlib.c` and registered via `cs_register_stdlib(vm)`.

### Core helpers

- `print(...)` → prints values to stdout
- `assert(cond, "message")` → sets a VM error and aborts execution if `cond` is falsy
- `typeof(x)` → returns `"nil" | "bool" | "int" | "float" | "string" | "list" | "map" | ...`
- `getenv("NAME")` → returns a string or `nil`

### Type predicates

- `is_nil(x)`, `is_bool(x)`, `is_int(x)`, `is_float(x)` → bool
- `is_string(x)`, `is_list(x)`, `is_map(x)`, `is_function(x)` → bool

### Multi-file loading

- `load(path)` → executes another script file (every call)
- `require(path)` → executes another script once per VM and returns its `exports` map
- `require_optional(path)` → like `require`, but returns `nil` if file doesn't exist (no error)

### Iteration

- `range(end)` → `[0, 1, ..., end-1]`
- `range(start, end)` → `[start, ..., end-1]`
- `range(start, end, step)` → `[start, start+step, ..., < end]` (step can be negative)

### List helpers

- `list()` → new list
- `len(list|string|map|strbuf)` → length
- `push(list, value)` → append
- `pop(list)` → pop last element (or `nil`)
- `insert(list, idx, value)` → insert (clamped)
- `remove(list, idx)` → remove and return element (or `nil`)
- `slice(list, start, end)` → sub-list

### Map helpers

- `map()` → new map
- `mget(map, key)` → get value (or `nil`)
- `mset(map, key, value)` → set value
- `mhas(map, key)` → bool
- `mdel(map, key)` → delete (bool)
- `keys(map)` → list of keys (strings)
- `values(map)` → list of values
- `items(map)` → list of `[key, value]` pairs
- `copy(list|map)` → shallow copy
- `deepcopy(list|map)` → deep copy (recursive)
- `reverse(list)` → reverse in-place
- `reversed(list)` → return reversed copy
- `contains(list|map|string, value|key)` → bool

Note: You can usually use indexing instead of `mget/mset`:
`m["k"]`, `m["k"] = v`.

### String helpers

- `str_find(s, sub)` → index (or `-1`)
- `str_replace(s, old, repl)` → new string
- `str_split(s, sep)` → list of strings
- `substr(s, start, len)` → substring
- `join(list, sep)` → join list elements into a string
- `str_trim(s)` → remove leading/trailing whitespace
- `str_ltrim(s)` → remove leading whitespace
- `str_rtrim(s)` → remove trailing whitespace
- `str_lower(s)` → convert to lowercase
- `str_upper(s)` → convert to uppercase
- `str_startswith(s, prefix)` → bool
- `str_endswith(s, suffix)` → bool
- `str_repeat(s, count)` → repeat string N times

### Path helpers

- `path_join(a, b)` → joined path (simple join)
- `path_dirname(path)` → directory portion
- `path_basename(path)` → basename
- `path_ext(path)` → extension without dot (or `""`)
Math functions

- `abs(x)` → absolute value (preserves type: int→int, float→float)
- `min(...values)`, `max(...values)` → minimum/maximum of numbers
- `floor(x)`, `ceil(x)`, `round(x)` → rounding to int
- `sqrt(x)` → square root (returns float)
- `pow(base, exp)` → exponentiation (returns float)

### Conversions

- `to_int(x)` → int (or `nil` if not convertible; floats truncate toward zero
- `fmt("x=%d s=%s b=%b v=%v", ...)` → formatted string
  - `%d` int, `%s` string, `%b` bool, `%v` any value (best-effort), `%%` literal percent

### Conversions

- `to_int(x)` → int (or `nil` if not convertible)
- `to_str(x)` → string

### Time helpers

- `now_ms()` → current wall-clock time in milliseconds (int)
- `sleep(ms)` → sleep for `ms` milliseconds (blocking; implemented via POSIX `nanosleep`)

### Error Objects

- `error(msg)` → create error object with message and stack trace
- `is_error(value)` → check if value is an error object

### GC (Garbage Collection)

- `gc()` → manually collect list/map reference cycles (returns number of objects collected)
- `gc_stats()` → returns map with GC statistics: `{tracked, collections, collected, allocations}`
- `gc_config()` → get current GC auto-collect configuration
- `gc_config(map)` → set GC config from map with `{threshold, alloc_trigger}`
- `gc_config(threshold, alloc_trigger)` → set both GC parameters

GC is a cycle collector for `list`/`map` containers. Auto-collect policies:
- **Threshold**: collect when `tracked >= threshold` (0 = disabled)
- **Alloc trigger**: collect every N allocations (0 = disabled)

Example:
```cs
gc_config(50, 25);  // collect at 50 tracked objects or every 25 allocations
let stats = gc_stats();
print("Collections:", stats["collections"], "Collected:", stats["collected"]);
```

### Safety Controls (Script-Level)

- `set_timeout(ms)` → set execution timeout in milliseconds
- `set_instruction_limit(count)` → set max instruction count
- `get_timeout()` → get current timeout (0 = unlimited)
- `get_instruction_limit()` → get current instruction limit (0 = unlimited)
- `get_instruction_count()` → get current instruction count

Scripts can configure their own safety limits to prevent runaway execution:
```cs
set_timeout(5000);              // 5 second timeout
set_instruction_limit(10000000); // 10M instructions
print("Running with", get_instruction_count(), "instructions so far");
```

### `strbuf` (string builder)

For high-performance string construction, use the native string builder:

```cs
let b = strbuf();
b.append("x");
b.append(123);
let s = b.str();
```

Methods:
- `b.append(x)` → appends a value (`string`, `int`, `bool`, `nil`)
- `b.str()` → returns a string snapshot
- `b.len()` → current length (bytes)
- `b.clear()` → clears the buffer

---

## Design Notes

- The runtime is split into:
  - **cs_value.c/h**: value representation and helpers for scalar and heap objects.
  - **cs_lexer.c/h** and **cs_parser.c/h**: tokenization and parsing into an AST.
  - **cs_vm.c/h**: execution engine with a small call/stack model and a global environment (`cs_env`) with lexical scoping via closures.
- **cs_stdlib.c**: small host-facing API exposed to CupidScript via `cs_register_native` (includes `print`, `assert`, `load`/`require`, list/map helpers, string/path helpers, and `fmt`).
- **Strings are ref-counted:** The code uses a dedicated `cs_string` type (see `cs_value.h`).
  *Note:* Ensure you consistently use the public API for string lifetimes.

---

## Error Reporting

Parse errors and runtime errors include `file:line:column` when available.

Runtime errors also include a stack trace, for example:

```text
Runtime error at examples/stacktrace.cs:3:15: division by zero
Stack trace:
  at inner (examples/stacktrace.cs:8:16)
  at outer (examples/stacktrace.cs:11:7)
```

Use `cs_vm_last_error(vm)` to retrieve the current VM error string (returns `""` if there is no error). `cs_last_error(vm)` is a compatibility getter that may return `NULL`.

---

## Examples

- `examples/test.cs` and `examples/stress.cs`: basic language coverage
- `tests/math.cs`: operator precedence checks (uses `assert`)
- `examples/features.cs`: modules (`require` exports), literals, loops, and stdlib helpers
- `examples/stacktrace.cs`: demonstrates runtime stack traces
- `examples/trycatch.cs`: demonstrates `throw` + `try/catch`
- `examples/throwtrace.cs`: uncaught throw stack trace
- `examples/closures.cs`: demonstrates closures + anonymous `fn() { }`
- `examples/time.cs`: demonstrates `now_ms()` and `sleep(ms)`
- `examples/benchmark.cs`: quick-and-dirty performance benchmark (arith/calls/list/map/string)
- `examples/gc_cycle.cs`: collects a list/map reference cycle using `gc()`
- `examples/gc_auto.cs`: demonstrates GC auto-collect with threshold and allocation triggers
- `examples/safety_config.cs`: demonstrates per-script safety control configuration
- `examples/safety_test.cs`: demonstrates safety limits stopping infinite loops
- `examples/classes.cs`: class definitions with inheritance
- `examples/structs.cs`: struct definitions and positional construction
- `examples/enums.cs`: enum definitions with named constants
- `examples/generators.cs`: generator functions with `yield`
- `examples/match_expr.cs`: pattern matching with `match` expressions
- `examples/match_patterns.cs`: advanced pattern matching with destructuring
- `examples/destructuring.cs`: destructuring assignment for lists, maps, tuples
- `examples/pipe_operator.cs`: pipe operator `|>` for function chaining
- `examples/spread_rest.cs`: spread `...` in literals and rest parameters
- `examples/arrow_functions.cs`: arrow function syntax `=>`
- `examples/default_params.cs`: default parameter values
- `examples/string_qol.cs`: string quality-of-life features (interpolation, raw strings)
- `examples/iteration_helpers.cs`: `filter`, `map_list`, `reduce`, `zip`, etc.
- `examples/regex.cs`: regular expression matching and replacement
- `examples/json.cs` and `examples/json_example.cs`: JSON parsing and stringification
- `examples/csv_demo.cs`: CSV parsing and generation
- `examples/xml_demo.cs`: XML parsing
- `examples/filesystem.cs`: file operations, directory listing
- `examples/glob_demo.cs`: file pattern matching with `glob()`
- `examples/subprocess_demo.cs`: running external processes
- `examples/http_client.cs`: HTTP requests (requires OpenSSL)

---

## Extending with Native Functions

- Implement a function matching `cs_native_fn` and register it:

  ```c
  static int my_native(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
      // Validate args, operate, and set *out as needed
      if (argc > 0) {
          // example: print first arg if string
      }
      if (out) *out = cs_nil();
      return 0;
  }
  ```

- Register from your bootstrap code:
  ```c
  cs_register_native(vm, "my.native", my_native, NULL);
  ```

- The layout for values and strings is defined in the headers.

### Native error handling pattern

Native functions return `0` on success. To raise an error:
- call `cs_error(vm, "message")`
- return non-zero from the native function

This produces a runtime error and unwinds execution (with a stack trace if the call happened from script).

### Callback pattern (CupidFM-style)

Because functions are values, a host can accept callbacks like:

```cs
fm.on("event", fn(payload) {
  print("event payload:", payload);
});
```

On the C side you typically:
- store the callback `cs_value` using `cs_value_copy`
- later invoke it with `cs_call_value`
- release it with `cs_value_release` when unregistering/unloading

---

## Notes

- This project is a small, embeddable scripting VM with a tiny standard library and some example natives.
- **License:** This project is licensed under the GNU General Public License version 3 (GPLv3).
  See the `COPYING` file or https://www.gnu.org/licenses/gpl-3.0.html for details.
- For a quick reference or usage guidance, check the header API in `src/cupidscript.h` and the implementation in the `src` directory.

---

## License

- **GPLv3.** This repository is licensed under the GNU General Public License version 3.
  See [`LICENSE`](LICENSE) or visit [https://www.gnu.org/licenses/gpl-3.0.html](https://www.gnu.org/licenses/gpl-3.0.html) for details.

---

## Known Issues
None currently tracked.

---

## Recent Updates

### YAML 1.2.2 Enhancements (2026-01)

**New Escape Sequences:**
- Added `\xHH` - 2-digit hex escape (0x00-0xFF)
- Added `\N` - Next line character (U+0085)
- Added `\_` - Non-breaking space (U+00A0)
- Added `\L` - Line separator (U+2028)
- Added `\P` - Paragraph separator (U+2029)

**Enhanced Validation:**
- Reserved indicators `@` and `` ` `` now properly rejected in plain scalars per YAML 1.2.2 spec

**Documentation:**
- Comprehensive escape sequence reference in [Data-Formats wiki](https://github.com/frankischilling/cupidscript/wiki/Data-Formats)
- Enhanced [Standard-Library](https://github.com/frankischilling/cupidscript/wiki/Standard-Library) YAML function documentation
- New example: `examples/yaml_escape_sequences.cs` demonstrating all escape sequences

**Testing:**
- Added 10 new escape sequence tests to `tests/yaml_advanced.cs` (now 50 tests)
- Created `tests/yaml_security.cs` with 25 comprehensive security and edge-case tests
- Total YAML test coverage: 75+ tests

**Compliance:**
- Full YAML 1.2.2 escape sequence support
- Improved error messages for invalid syntax
- Better handling of edge cases (empty documents, deep nesting, special characters)

See the [Data-Formats](https://github.com/frankischilling/cupidscript/wiki/Data-Formats) wiki page for complete YAML documentation.

---

## Contributing

- If you'd like to contribute:
  - Start by adding a README-style doc
  - Improve the build/tests
  - Provide small example scripts in the `examples` directory

---

## TODO

- [ ] Add cupidfm lib support

List and map literals accept a trailing comma, including nested literals. `str_repeat` returns an empty string for an empty input or a nonpositive count, and reports an error if the result size overflows.
