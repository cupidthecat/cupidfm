# Standard Library

## Table of Contents

- [IO / Introspection](#io--introspection)
- [Loading / Modules](#loading--modules)
- [Error Handling](#error-handling)
- [Constructors](#constructors)
- [Length](#length)
- [Iteration](#iteration)
- [List Ops](#list-ops)
- [Map Ops](#map-ops)
- [Set Ops](#set-ops)
- [Data Quality-of-Life](#data-quality-of-life)
- [String Ops](#string-ops)
- [List Utilities](#list-utilities)
- [Advanced List Operations](#advanced-list-operations)
- [Random Utilities](#random-utilities)
- [String Padding and Formatting](#string-padding-and-formatting)
- [Path Ops](#path-ops)
- [Date / Time](#date--time)
- [Filesystem](#filesystem)
- [Advanced File Operations](#advanced-file-operations-linux-only)
- [Formatting](#formatting)
- [JSON](#json)
- [Data Format Functions](#data-format-functions)
- [Time](#time)
- [Promises](#promises)
- [Math Functions](#math-functions)
- [Math Constants](#math-constants)
- [Conversion](#conversion)
- [Memory Management](#memory-management)
- [Safety Controls](#safety-controls)
- [Network I/O](#network-io)

These functions are registered by `cs_register_stdlib(vm)`.

## IO / Introspection

### `print(...values)`

Prints values separated by spaces and ends with newline.

### `typeof(value) -> string`

Returns one of:
`"nil" "bool" "int" "float" "string" "bytes" "list" "map" "set" "strbuf" "range" "function" "native" "promise"`

### Type Predicates

Boolean type-checking functions:

* `is_nil(value) -> bool`
* `is_bool(value) -> bool`
* `is_int(value) -> bool`
* `is_float(value) -> bool`
* `is_string(value) -> bool`
* `is_set(value) -> bool`
* `is_bytes(value) -> bool`
* `is_list(value) -> bool`
* `is_map(value) -> bool`
* `is_function(value) -> bool`
* `is_promise(value) -> bool`

Example:

```c
if (is_int(x) || is_float(x)) {
  print("x is a number");
}
```

### `getenv(key: string) -> string | nil`

Returns environment variable value or `nil` if missing.

### `assert(cond, message?)`

* If `cond` is falsy → runtime error
* Optional second argument string overrides error message

### `subprocess(cmd: string, args?: list) -> map`

Runs an external command and captures combined stdout/stderr.

Returns a map:

* `code` - exit code (int)
* `out` - captured output (string)

Notes:

* `args` is an optional list of arguments.
* Output is captured as raw bytes into a string.

Example:

```c
let r = subprocess("echo", ["hello"]);
print(r.code);       // 0
print(r.out);        // "hello\n"
```

## Loading / Modules

### `load(path: string) -> bool`

Runs a file. Returns `true` on success, `nil`/error on failure.

### `require(path: string) -> value`

Runs a file once per resolved path.

* Subsequent calls with the same resolved path return the cached exports without re-running.
* Returns the module's `exports` map.

### `require_optional(path: string) -> value | nil`

Like `require()`, but returns `nil` if the file doesn't exist instead of throwing an error.

If the file exists, it behaves like `require()` and returns the module exports.

## Error Handling

### `error(msg: string) -> map`

### `error(msg: string, code: string) -> map`

Creates a standardized error object with:

* `msg` - Error message
* `code` - Error code (defaults to `"ERROR"` if omitted)
* `stack` - List of stack frame strings

### `is_error(value) -> bool`

Returns `true` if value is an error object (has `msg` and `code` fields).

Example:

```c
fn validate(x) {
  if (x < 0) {
    throw error("Invalid value: must be non-negative", "INVALID_ARG");
  }
}

try {
  validate(-5);
} catch (e) {
  if (is_error(e)) {
    print("Error:", e.msg);
    print("Code:", e.code);
  }
}
```

### `format_error(err) -> string`

Formats an error object (as produced by `error()`) into a human-readable string.

Current format is a single line: `[CODE] message`. Stack traces are available on `err.stack`.

### `ERR -> map`

Global map of common error codes (strings), intended to be used with `error(msg, code)`.

## Constructors

### `list() -> list`

### `map() -> map`

### `set()` / `set(list|map|set)`

Creates a set (unique collection of values).

* no args → empty set
* list → adds each element (duplicates ignored)
* map → adds each key
* set → copies values

### `strbuf() -> strbuf`

### `bytes()` / `bytes(x)`

Creates a bytes buffer.

Accepted inputs:

* no args → empty bytes
* int → length (zero-filled)
* string → raw byte copy of the string
* list of ints 0..255 → bytes filled from list
* bytes → returns a copy

## Length

### `len(x) -> int`

Works for:

* string: number of bytes
* bytes: byte length
* list: element count
* map: entry count
* set: entry count
* strbuf: byte length
  Else returns 0.

## Iteration

### `range(end)` / `range(start, end)` / `range(start, end, step) -> range`

Returns a lazy range object (iterable) without allocating a list:

* `range(5)` → yields `0, 1, 2, 3, 4`
* `range(2, 7)` → yields `2, 3, 4, 5, 6`
* `range(0, 10, 2)` → yields `0, 2, 4, 6, 8`
* `range(10, 0, -2)` → yields `10, 8, 6, 4, 2`

Default step is `1`. Step cannot be `0`.

Example:

```c
for i in range(10) {
  print(i);
}
```

### `enumerate(list) -> list`

Returns a list of `[index, value]` pairs. Use with destructuring for Pythonic iteration:

```cs
let items = ["a", "b", "c"]

// Pythonic style with destructuring
for [idx, val] in enumerate(items) {
    print("${idx}: ${val}")
    // Output: 0: a, 1: b, 2: c
}

// Native two-variable syntax (VALUE FIRST, then INDEX)
for val, idx in items {
    print("${idx}: ${val}")
    // Output: 0: a, 1: b, 2: c
}

// Manual destructuring
let pairs = enumerate(items)
for pair in pairs {
    let [idx, val] = pair
    print("${idx}: ${val}")
}
```

**Important:** CupidScript's native `for val, idx in list` syntax uses **value-first** order, while `enumerate()` returns **index-first** `[idx, val]` pairs to match Python conventions. Use the appropriate syntax for your use case.

**Examples:**

```c
let xs = ["a", "b", "c"];
print(enumerate(xs)); // [[0, "a"], [1, "b"], [2, "c"]]

// Use with destructuring in comprehensions
let labeled = [to_str(i) + ": " + v for [i, v] in enumerate(xs)];
// ["0: a", "1: b", "2: c"]

// Create index-to-value map
let index_map = {i: v for [i, v] in enumerate(xs)};
// {0: "a", 1: "b", 2: "c"}

// Filter based on index
let odd_indices = [v for [i, v] in enumerate(xs) if i % 2 != 0];
// ["b"]

// Native syntax: value comes first
for val, idx in xs {
    print("Item ${idx} is ${val}");
    // Item 0 is a
    // Item 1 is b
    // Item 2 is c
}
```

**Iteration Order Summary:**
| Syntax | Order | Example |
|--------|-------|---------|
| `for val, idx in list` | Value, Index | `for x, i in [10, 20]` → x=10,i=0 then x=20,i=1 |
| `enumerate(list)` | `[Index, Value]` | `enumerate([10, 20])` → `[[0, 10], [1, 20]]` |
| `for [idx, val] in enumerate(list)` | Index, Value (destructured) | Matches Python's `enumerate()` |

**Best Practice:** 
- Use native `for val, idx in list` when you primarily need the value
- Use `enumerate()` with destructuring when porting Python code or when you want explicit index-first ordering

### `zip(listA, listB) -> list`

Pairs elements by index up to the shorter list length.

```c
print(zip([1, 2], ["x", "y", "z"])); // [[1, "x"], [2, "y"]]
```

### `any(list, [pred]) -> bool`

Returns `true` if any element is truthy (or satisfies `pred`).

```c
any([nil, 0, 3]) // true
any([1, 3, 5], fn(x) => x % 2 == 0) // false
```

### `all(list, [pred]) -> bool`

Returns `true` if all elements are truthy (or satisfy `pred`).

```c
all([1, 2, 3]) // true
all([1, 2, 3], fn(x) => x < 3) // false
```

### `filter(list, pred) -> list`

Returns a new list with elements that satisfy `pred`.

```c
filter([1, 2, 3, 4], fn(x) => x % 2 == 0) // [2, 4]
```

### `map(list, mapper) -> list`

Returns a new list with `mapper` applied to each element.

```c
map([1, 2, 3], fn(x) => x * 2) // [2, 4, 6]
```

### `reduce(list, reducer, [init]) -> value`

Reduces a list left-to-right. If `init` is omitted, uses the first element.

```c
reduce([1, 2, 3], fn(a, b) => a + b) // 6
reduce([1, 2, 3], fn(a, b) => a + b, 10) // 16
```

### `insert(list, index: int, value)`

Inserts `value` at position `index`, shifting existing elements.

### `remove(list, index: int) -> value | nil`

Removes and returns the element at `index`.

### `slice(list, start: int, length: int) -> list`

Returns a new list containing `length` elements starting at `start`.

## Map Ops

### `mget(map, key: value) -> value | nil`

### `mset(map, key: value, value)`

### `mhas(map, key: value) -> bool`

### `mdel(map, key: value)`

Deletes a key from the map.

### `keys(map) -> list[value]`

### `values(map) -> list[value]`

Returns a list of all values in the map.

### `items(map) -> list[list]`

Returns a list of `[key, value]` pairs.

## Set Ops

CupidScript provides both modern literal syntax and legacy function-based operations for sets.

### Modern Syntax (Recommended)

```c
// Create sets
let s = #{1, 2, 3};
let empty = #{};

// Set comprehensions
let evens = #{x for x in range(20) if x % 2 == 0};

// Methods
s.add(4);           // returns true if added
s.contains(3);      // true
s.remove(2);        // returns true if removed
s.size();           // 3
s.clear();          // empties the set

// Operators
let a = #{1, 2, 3};
let b = #{2, 3, 4};
a | b;  // union: #{1, 2, 3, 4}
a & b;  // intersection: #{2, 3}
a - b;  // difference: #{1}
a ^ b;  // symmetric difference: #{1, 4}
```

See [Collections - Sets](Collections#sets) for complete documentation.

### Legacy Functions

These functions remain available for compatibility:

### `set_add(set, value) -> bool`

Adds a value. Returns `true` if inserted, `false` if it was already present.

### `set_has(set, value) -> bool`

Returns `true` if value exists.

### `set_del(set, value) -> bool`

Removes a value. Returns `true` if removed.

### `set_values(set) -> list`

Returns a list of values in the set (iteration order is unspecified).

### `map_values(map) -> list[value]`

Alias for `values()`. Returns all values as a list for convenient iteration.

## Data Quality-of-Life

### `copy(x) -> value`

Returns a shallow copy:

* Lists: creates new list with same elements
* Maps: creates new map with same key-value pairs
* Sets: creates new set with same values
* Other types: returns the value as-is

### `deepcopy(x) -> value`

Returns a deep copy with cycle detection:

* Lists, maps, and sets are recursively copied
* Handles self-referential structures
* Other types: returns the value as-is

### `reverse(list)`

Reverses a list in-place.

### `reversed(list) -> list`

Returns a new reversed list without modifying the original.

### `contains(container, item) -> bool`

Checks if item exists:

* Lists: checks for element
* Maps: checks for key
* Sets: checks for value
* Strings: checks for substring

Example:

```c
let original = [1, [2, 3], 4];
let shallow = copy(original);
let deep = deepcopy(original);

shallow[1][0] = 99;  // Modifies original[1][0]
deep[1][1] = 88;     // Does not affect original

reverse([1, 2, 3]);  // Returns [3, 2, 1], in-place
let rev = reversed([1, 2, 3]);  // original unchanged

print(contains([1, 2, 3], 2));        // true
print(contains({"a": 1}, "a"));       // true
print(contains(set([1, 2]), 2));      // true
print(contains("hello", "ell"));      // true
```

## String Ops

### `str_find(s: string, sub: string) -> int`

Returns first index or `-1`.

### `str_replace(s, old, rep) -> string`

Replaces all occurrences of `old` with `rep`.

* If `old` is empty, returns original string.

### `str_split(s, sep) -> list[string]`

Splits on a string separator.

* If `sep` is empty, returns `[s]`.

### `str_contains(s: string, sub: string) -> bool`

Returns `true` if `sub` appears anywhere in `s`.

### `str_count(s: string, sub: string) -> int`

Counts non-overlapping occurrences of `sub`. If `sub` is empty, returns 0.

### `str_pad_start(s: string, width: int, pad: string = " ") -> string`

Pads on the left until `width` is reached.

### `str_pad_end(s: string, width: int, pad: string = " ") -> string`

Pads on the right until `width` is reached.

### `str_reverse(s: string) -> string`

Returns the reversed string (byte-wise).

### Regex

Regex uses POSIX Extended Regular Expressions (ERE). Invalid patterns raise a runtime error. On Windows builds, regex functions may be unavailable.

#### `regex_is_match(pattern: string, text: string) -> bool`

Returns `true` if any match is found.

#### `regex_match(pattern: string, text: string) -> bool`

Returns `true` only if the *entire* string matches.

#### `regex_find(pattern: string, text: string) -> map | nil`

Returns a match object or `nil` if no match:

* `start` (int) - byte index of match start
* `end` (int) - byte index of match end
* `match` (string) - matched text
* `groups` (list) - capture groups (`nil` for unmatched groups)

#### `regex_find_all(pattern: string, text: string) -> list[map]`

Returns a list of match objects with the same shape as `regex_find`.

#### `regex_replace(pattern: string, text: string, replacement: string) -> string`

Replaces all matches with a literal replacement string. Capture expansion (like `$1`) is not supported.

Example:

```c
let text = "foo-123-bar";
print(regex_is_match("[0-9]+", text));

let m = regex_find("([0-9]+)", text);
print(m.start, m.end, m["match"], m.groups[0]);

let all = regex_find_all("[0-9]+", "a1 b22 c333");
print(len(all));

print(regex_replace("[0-9]+", "a1b22c", "#"));
```

### `substr(s: string, start: int, length: int) -> string`

Returns substring starting at `start` with `length` characters.

### `join(list, separator: string) -> string`

Joins list elements into a string with the given separator.

## List Utilities

### `list_unique(list) -> list`

Returns a new list with duplicates removed (preserves order).

### `list_flatten(list) -> list`

Flattens one level of nested lists.

### `list_chunk(list, size: int) -> list[list]`

Splits into sublists of at most `size` elements.

### `list_compact(list) -> list`

Removes `nil` values.

### `list_sum(list) -> int | float | nil`

Sums numeric elements (ignores `nil`). Returns `nil` if any non-number is present.

## Advanced List Operations

### `list_intersection(list1, list2) -> list`

Returns a new list containing elements that appear in both lists (no duplicates).

Example:

```c
let a = [1, 2, 3, 4];
let b = [3, 4, 5, 6];
print(list_intersection(a, b));  // [3, 4]
```

### `list_difference(list1, list2) -> list`

Returns a new list containing elements in `list1` that are not in `list2`.

Example:

```c
let a = [1, 2, 3, 4];
let b = [3, 4, 5, 6];
print(list_difference(a, b));  // [1, 2]
```

### `list_union(list1, list2) -> list`

Returns a new list containing all unique elements from both lists.

Example:

```c
let a = [1, 2, 3];
let b = [3, 4, 5];
print(list_union(a, b));  // [1, 2, 3, 4, 5]
```

### `list_partition(list, predicate: function) -> [list, list]`

Splits a list into two lists based on a predicate function.

Returns `[truthy_list, falsy_list]` where:
* `truthy_list` contains items for which `predicate(item)` is truthy
* `falsy_list` contains items for which `predicate(item)` is falsy

Example:

```c
fn is_even(x) { return x % 2 == 0; }
let numbers = [1, 2, 3, 4, 5, 6];
let parts = list_partition(numbers, is_even);
print(parts[0]);  // [2, 4, 6] - even numbers
print(parts[1]);  // [1, 3, 5] - odd numbers
```

### `take(list, n: int) -> list`

Returns a new list containing the first `n` elements.

Example:

```c
let items = [1, 2, 3, 4, 5];
print(take(items, 3));  // [1, 2, 3]
print(take(items, 10)); // [1, 2, 3, 4, 5] - takes all available
```

### `drop(list, n: int) -> list`

Returns a new list with the first `n` elements removed.

Example:

```c
let items = [1, 2, 3, 4, 5];
print(drop(items, 2));  // [3, 4, 5]
print(drop(items, 10)); // [] - drops all
```

## Random Utilities

### `random_choice(list) -> value | nil`

Returns a random element from the list, or `nil` if the list is empty.

Example:

```c
let prizes = ["Car", "TV", "Hat"];
let winner = random_choice(prizes);
print("You won: " + winner);
```

### `shuffle(list) -> nil`

Randomly reorders the elements of the list **in place** using Fisher-Yates shuffle.

Example:

```c
let deck = [1, 2, 3, 4, 5];
shuffle(deck);
print(deck);  // e.g., [3, 1, 5, 2, 4]
```

### `random_int(min: int, max: int) -> int`

Returns a random integer between `min` and `max` (inclusive).

Example:

```c
let dice_roll = random_int(1, 6);
print("You rolled: " + to_str(dice_roll));
```

## String Padding and Formatting

Functions for formatting strings with padding and alignment.

### `str_pad_left(s: string, width: int, fill?: string) -> string`

Pads the string on the left with spaces (or a custom fill character) to reach the desired width.

* If `width` is less than or equal to the string length, returns the original string unchanged.
* Optional third parameter specifies the fill character (default is space `" "`).

Example:

```c
print(str_pad_left("42", 5));       // "   42"
print(str_pad_left("42", 5, "0"));  // "00042"
print(str_pad_left("hello", 3));    // "hello" (no padding)
```

### `str_pad_right(s: string, width: int, fill?: string) -> string`

Pads the string on the right with spaces (or a custom fill character) to reach the desired width.

* If `width` is less than or equal to the string length, returns the original string unchanged.
* Optional third parameter specifies the fill character (default is space `" "`).

Example:

```c
print(str_pad_right("42", 5));       // "42   "
print(str_pad_right("42", 5, "-"));  // "42---"
print(str_pad_right("hello", 3));    // "hello" (no padding)
```

## String Ergonomics

### `str_trim(s: string) -> string` / `trim(s: string) -> string`

Removes leading and trailing whitespace.

### `str_ltrim(s: string) -> string` / `ltrim(s: string) -> string`

Removes leading whitespace only.

### `str_rtrim(s: string) -> string` / `rtrim(s: string) -> string`

Removes trailing whitespace only.

### `str_lower(s: string) -> string` / `lower(s: string) -> string`

Converts string to lowercase (ASCII only).

### `str_upper(s: string) -> string` / `upper(s: string) -> string`

Converts string to uppercase (ASCII only).

### `str_startswith(s: string, prefix: string) -> bool` / `starts_with(s, prefix) -> bool`

Returns `true` if string starts with the given prefix.

### `str_endswith(s: string, suffix: string) -> bool` / `ends_with(s, suffix) -> bool`

Returns `true` if string ends with the given suffix.

### `str_repeat(s: string, count: int) -> string`

Returns a new string with `s` repeated `count` times.

### `split_lines(s: string) -> list[string]`

Splits the string on `\n` or `\r\n`. An empty trailing line is preserved.

#### `str_pad_left(s: string, width: int, fill?: string) -> string`

Pads the string on the left with spaces (or a custom fill character) to reach the desired width.

* If `width` is less than or equal to the string length, returns the original string unchanged.
* Optional third parameter specifies the fill character (default is space `" "`).

Example:

```c
print(str_pad_left("42", 5));       // "   42"
print(str_pad_left("42", 5, "0"));  // "00042"
print(str_pad_left("hello", 3));    // "hello" (no padding)
```

#### `str_pad_right(s: string, width: int, fill?: string) -> string`

Pads the string on the right with spaces (or a custom fill character) to reach the desired width.

* If `width` is less than or equal to the string length, returns the original string unchanged.
* Optional third parameter specifies the fill character (default is space `" "`).

Example:

```c
print(str_pad_right("42", 5));       // "42   "
print(str_pad_right("42", 5, "-"));  // "42---"
print(str_pad_right("hello", 3));    // "hello" (no padding)
```

Example:

```c
let text = "  Hello World  ";
print(str_trim(text));           // "Hello World"
print(str_lower("HELLO"));       // "hello"
print(str_upper("hello"));       // "HELLO"
print(str_startswith("test", "te"));  // true
print(str_endswith("test", "st"));    // true
print(str_repeat("ab", 3));      // "ababab"
print(split_lines("a\n b\r\n")); // ["a", " b", ""]
```

## Path Ops

### `path_join(a: string, b: string) -> string`

Joins with `/` when needed.

### `path_dirname(path: string) -> string`

Returns directory portion:

* no slash → `.`

## Date / Time

### `unix_ms() -> int`

Returns the current Unix time in milliseconds.

### `unix_s() -> int`

Returns the current Unix time in seconds.

### `datetime_now() -> map`

Returns local time as a map with fields:

* `year`, `month`, `day`
* `hour`, `minute`, `second`, `ms`
* `wday` (0=Sunday), `yday` (1-366)
* `is_dst` (bool), `is_utc` (bool)

### `datetime_utc() -> map`

Same as `datetime_now()` but in UTC (`is_utc = true`).

### `datetime_from_unix_ms(ms: int|float) -> map`

Converts a Unix milliseconds timestamp to local time.

### `datetime_from_unix_ms_utc(ms: int|float) -> map`

Converts a Unix milliseconds timestamp to UTC.

Example:

```c
let ms = unix_ms();
let local = datetime_now();
let utc = datetime_utc();
let epoch = datetime_from_unix_ms_utc(0);
print(ms, local, utc, epoch);
```
* `/x` → `/`
* `a/b` → `a`

### `path_basename(path: string) -> string`

Returns last path component.

### `path_ext(path: string) -> string`

Returns extension without dot, or `""` if none.

## Filesystem

### `read_file(path: string) -> string | nil`

Reads a file and returns its contents as a string. Returns `nil` if the file can't be read.

### `read_file_bytes(path: string) -> bytes | nil`

Reads a file and returns its raw contents as bytes. Returns `nil` if the file can't be read.

### `write_file(path: string, data: string|bytes) -> bool`

Writes `data` to `path` (overwrites). Returns `true` on success.

### `write_file_bytes(path: string, data: bytes) -> bool`

Writes raw bytes to `path` (overwrites). Returns `true` on success.

### `exists(path: string) -> bool`

Returns `true` if the path exists.

### `is_dir(path: string) -> bool`

Returns `true` if the path exists and is a directory.

### `is_file(path: string) -> bool`

Returns `true` if the path exists and is a regular file.

### `list_dir(path: string) -> list | nil`

Returns a list of entry names (not full paths). Returns `nil` if the directory can't be opened.

### `mkdir(path: string) -> bool`

Creates a directory. Returns `true` on success (or if it already exists).

### `rm(path: string) -> bool`

Removes a file or an empty directory. Returns `true` on success.

### `rename(from: string, to: string) -> bool`

Renames or moves a file or directory. Returns `true` on success.

### `cwd() -> string`

Returns the VM's current working directory (used for resolving relative paths).

### `chdir(path: string) -> bool`

Sets the VM's current working directory for future relative paths. Returns `true` on success.

## Advanced File Operations (Linux only)

> **Note:** These features are available on Linux only. See [File-Handling](File-Handling.md) for detailed documentation and examples.

### Glob Patterns

#### `glob(pattern: string, path?: string) -> list`

Matches files using glob patterns with wildcards.

Supported patterns:
* `*` - Match any characters except `/`
* `?` - Match single character
* `**` - Match recursively
* `[abc]` - Match any char in set
* `{a,b,c}` - Match any alternative

Example:
```cs
// Find all text files
let txt_files = glob("*.txt");

// Find all CupidScript files recursively
let cs_files = glob("**/*.cs", "src");

// Find test files
let tests = glob("test_*.cs", "tests");
```

### File Watching

Monitor files and directories for changes using inotify.

#### `watch_file(path: string, callback: function) -> int`

Watch a file for changes. Returns watch handle.

Callback signature: `fn(event_type: string, path: string)`

Event types: `"created"`, `"modified"`, `"deleted"`, `"renamed"`

#### `watch_dir(path: string, callback: function, recursive?: bool) -> int`

Watch a directory for changes. Returns watch handle.

Set `recursive` to `true` to watch subdirectories.

#### `unwatch(handle: int) -> bool`

Stop watching a file or directory. Returns `true` on success.

#### `process_file_watches() -> nil`

Process pending file system events. Call this periodically to trigger callbacks.

Example:
```cs
// Watch configuration file
let handle = watch_file("config.yaml", fn(event, path) {
    print("Config changed:", event);
});

// Main loop
while running {
    process_file_watches();
    sleep(100);
}

unwatch(handle);
```

### Temporary Files

Create temporary files and directories with automatic cleanup on exit.

#### `temp_file(prefix?: string, suffix?: string) -> string`

Create a temporary file. Returns path to file.

Example:
```cs
let temp = temp_file("myapp_", ".txt");
write_file(temp, "temporary data");
// Automatically cleaned up on exit
```

#### `temp_dir(prefix?: string) -> string`

Create a temporary directory. Returns path to directory.

Example:
```cs
let workspace = temp_dir("build_");
mkdir(workspace + "/obj");
write_file(workspace + "/obj/main.o", "...");
// Entire tree cleaned up on exit
```

### Archive Operations

Create and extract tar and gzip archives.

#### `gzip_compress(input_path: string, output_path: string) -> bool`

Compress a file using gzip. Returns `true` on success.

#### `gzip_decompress(input_path: string, output_path: string) -> bool`

Decompress a gzip file. Returns `true` on success.

Example:
```cs
gzip_compress("large.txt", "large.txt.gz");
gzip_decompress("large.txt.gz", "restored.txt");
```

#### `tar_create(archive_path: string, files: list, compress?: string) -> bool`

Create a tar archive from a list of files.

Set `compress` to `"gzip"` to create a `.tar.gz` file.

#### `tar_list(archive_path: string) -> list | nil`

List contents of a tar archive. Returns list of filenames or `nil` on error.

#### `tar_extract(archive_path: string, dest_path: string) -> bool`

Extract a tar archive to a directory. Returns `true` on success.

Example:
```cs
// Create tar archive
let files = ["file1.txt", "file2.txt", "file3.cs"];
tar_create("backup.tar", files);

// List contents
let contents = tar_list("backup.tar");

// Extract
tar_extract("backup.tar", "restored");

// Create compressed archive
tar_create("backup.tar.gz", files, "gzip");
```

For comprehensive documentation, examples, and best practices, see [File-Handling](File-Handling.md).

## Formatting

### `fmt(format_string, ...args) -> string`

Supported specifiers:

* `%d` expects int
* `%b` expects bool
* `%s` expects string
* `%v` accepts any value (uses a generic repr)

Literal percent: `%%`

Example:

```c
print(fmt("x=%d ok=%b name=%s", 10, true, "frank"));
```

## JSON

### `json_parse(text: string) -> value`

Parses JSON into CupidScript values, fully conforming to [RFC 8259](https://www.rfc-editor.org/rfc/rfc8259):

* `null` → `nil`
* `true/false` → `bool`
* numbers → `int` or `float`
* strings → `string` (all Unicode escapes, including surrogate pairs, are decoded to UTF-8)
* arrays → `list`
* objects → `map` (object keys must be strings; non-string keys are rejected)

#### Unicode

All Unicode escapes (`\uXXXX`) are decoded, including surrogate pairs for code points above U+FFFF. Invalid or incomplete surrogate pairs are rejected. CupidScript strings are always valid UTF-8.

#### Object Keys

Object keys must be strings. Parsing or stringifying a map with non-string keys will result in an error, as required by RFC 8259.

### `json_stringify(value) -> string`

Serializes a value to JSON. Supports `nil`, `bool`, `int`, `float`, `string`, `list`, `map` (with string keys only). Produces strict RFC 8259-compliant JSON.

Example:

```c
let obj = {"name": "frank", "age": 30, "tags": ["a", "b"]};
let s = json_stringify(obj);
let back = json_parse(s);
print(back.name);
```

See also: [JSON Unicode and Surrogate Example](../examples/json_unicode_example.cs)

## Data Format Functions

CupidScript supports CSV, YAML, and XML data formats. See [Data-Formats](Data-Formats.md) for detailed usage and examples.

### CSV Functions

#### `csv_parse(text: string, options?: map) -> list`

Parses CSV text into a list of lists (rows). Each row is a list of string fields.

Options:
* `delimiter` - Field separator (default: `","`)
* `quote` - Quote character (default: `"\""`)
* `headers` - Treat first row as headers, return list of maps (default: `false`)
* `skip_empty` - Skip empty rows (default: `false`)
* `trim` - Trim whitespace from fields (default: `false`)

Example:
```cs
let csv = "name,age,city\nJohn,30,NYC\nAlice,25,LA";
let rows = csv_parse(csv);
// Returns: [["name", "age", "city"], ["John", "30", "NYC"], ["Alice", "25", "LA"]]

// With headers
let records = csv_parse(csv, {headers: true});
// Returns: [{"name": "John", "age": "30", "city": "NYC"}, ...]
```

#### `csv_stringify(rows: list, options?: map) -> string`

Converts a list of lists (or list of maps) to CSV format.

Options:
* `delimiter` - Field separator (default: `","`)
* `quote` - Quote character (default: `"\""`)

Example:
```cs
let data = [["name", "age"], ["Bob", "35"]];
let csv = csv_stringify(data);
// Returns: "name,age\nBob,35\n"
```

### YAML Functions

#### `yaml_parse(text: string) -> value | nil`

Parses YAML 1.2.2 compliant text into CupidScript values.

**Parameters:**
* `text` (string) - YAML-formatted text to parse

**Returns:**
* Parsed value (map, list, string, number, boolean, or nil)
* `nil` on parse error (error message is set)

**Supported Features:**

*Scalars:*
* Plain strings, quoted strings (single `'...'` or double `"..."`)
* Integers: `123`, `-456`, `0xFF` (hex), `0o777` (octal)
* Floats: `3.14`, `-2.5`, `1e3` (scientific), `.inf`, `-.inf`, `.nan`
* Booleans: `true`, `false`, `yes`, `no`, `on`, `off`
* Null: `null`, `~`, or empty value

*Collections:*
* Maps (key-value): block or flow style `{key: value}`
* Lists: block or flow style `[item1, item2]`

*Advanced Features:*
* **Block scalars:** `|` (literal), `>` (folded)
* **Chomping:** `|-` (strip), `|+` (keep), `|` (clip)
* **Explicit indentation:** `|2`, `>4`
* **Type tags:** `!!str`, `!!int`, `!!float`, `!!bool`, `!!null`
* **Anchors/aliases:** `&anchor`, `*alias`
* **Merge keys:** `<<: *anchor`
* **Explicit keys:** `? key`
* **Multi-document:** `---` separator
* **Directives:** `%YAML 1.2`, `%TAG`

*Escape Sequences (double-quoted strings only):*
* **Common:** `\\`, `\"`, `\0`, `\t`, `\n`, `\r`, `\a`, `\b`, `\v`, `\f`, `\e`, `\ `
* **Hex:** `\xHH` (2 hex digits, e.g., `\x41` → A)
* **Unicode:** `\uXXXX` (4 hex digits), `\UXXXXXXXX` (8 hex digits)
* **Special (YAML 1.2.2):** `\N` (next line), `\_` (NBSP), `\L` (line separator), `\P` (paragraph separator)

**Error Conditions:**
* Invalid indentation (tabs in block scalars, inconsistent spacing)
* Reserved indicators `@` or `` ` `` starting plain scalars
* Unterminated quoted strings
* Invalid escape sequences
* Undefined aliases
* Malformed tags
* Syntax errors in flow collections

**Examples:**

```cs
// Basic parsing
let yaml = "name: MyApp\nversion: 1.0.0\ndebug: true";
let config = yaml_parse(yaml);
print(config.name);     // "MyApp"
print(config.debug);    // true

// Escape sequences
let escaped = yaml_parse("text: \"Line1\\nLine2\\x20Tab\\there\"\n");
print(escaped.text);    // "Line1\nLine2 Tab\there"

// Advanced features
let advanced = yaml_parse("
text: |-
  no trailing newlines
value: !!str 123
base: &base
  x: 1
  y: 2
derived:
  <<: *base
  y: 99
");
print(advanced.derived.x);  // 1 (from base)
print(advanced.derived.y);  // 99 (overridden)

// Error handling
let bad = yaml_parse("invalid: [unclosed");
if (bad == nil) {
  print("Parse error occurred");
}
```

**Performance:**
* Optimized for config files up to 1MB
* Large files (> 10MB) may be slow
* Deep nesting (> 100 levels) supported but impacts performance

**See Also:** [Data-Formats](Data-Formats) for comprehensive YAML guide

#### `yaml_parse_all(text: string) -> list | nil`

Parses multiple YAML documents separated by `---` from a single string.

**Parameters:**
* `text` (string) - Multi-document YAML text

**Returns:**
* List of parsed documents
* Empty list `[]` if input contains no documents
* `nil` on parse error

**Example:**
```cs
let docs = yaml_parse_all("
---
name: Config
version: 1.0
...
---
name: Data
items: [a, b, c]
---
name: Metadata
");

print(len(docs));      // 3
print(docs[0].name);   // "Config"
print(docs[1].name);   // "Data"
print(docs[2].name);   // "Metadata"

// Single document also works
let single = yaml_parse_all("key: value");
print(len(single));    // 1
print(single[0].key);  // "value"
```

**Note:** The explicit document end marker `...` is optional.

#### `yaml_stringify(value, indent?: int) -> string`

Converts a CupidScript value to YAML 1.2.2 format.

**Parameters:**
* `value` (any) - Value to serialize (map, list, string, number, boolean, nil)
* `indent` (int, optional) - Indentation spaces (default: 2, range: 0-8)

**Returns:**
* YAML-formatted string in block style

**Serialization Rules:**
* Maps → YAML mappings (key: value)
* Lists → YAML sequences (- item)
* Strings → plain or quoted (auto-detected)
* Numbers → numeric scalars
* Booleans → `true` or `false`
* `nil` → `null`
* Special characters in strings trigger quoting
* Reserved indicators `@` and `` ` `` are quoted

**Example:**
```cs
let data = {"host": "localhost", "port": 8080, "tags": ["a", "b"]};
let yaml = yaml_stringify(data);
// Returns block-style YAML:
// host: localhost
// port: 8080
// tags:
//   - a
//   - b

// Custom indentation
let yaml4 = yaml_stringify(data, 4);
// host:    localhost
// port:    8080
// tags:
//     -   a
//     -   b

// Handles special characters
let special = {"path": "C:\\Users", "at": "@value"};
let yaml_special = yaml_stringify(special);
// path: "C:\\Users"
// at: "@value"
```

**Limitations:**
* Does not generate anchors/aliases (use explicit YAML)
* Does not generate tags (all values use implicit typing)
* Always uses block style (not flow style)

**See Also:** [Data-Formats](Data-Formats) for YAML details

### XML Functions

#### `xml_parse(text: string) -> map`

Parses XML into a map structure:
```cs
{
  "name": "element-name",
  "attrs": {"attr1": "value1", ...},  // Optional
  "text": "text content",              // Optional
  "children": [...]                    // Optional
}
```

Features:
* Elements, attributes, text content
* Self-closing tags: `<br/>`
* CDATA sections
* Entity decoding: `&lt;`, `&gt;`, `&amp;`, `&quot;`, `&apos;`
* Numeric entities: `&#65;`, `&#x41;`
* Comments (skipped)
* Processing instructions (skipped)

Example:
```cs
let xml = "<person name=\"John\" age=\"30\"><city>NYC</city></person>";
let doc = xml_parse(xml);
print(doc.name);           // "person"
print(doc.attrs.name);     // "John"
print(doc.children[0].text);  // "NYC"
```

#### `xml_stringify(element: map, indent?: int) -> string`

Converts an element map to XML format.

Parameters:
* `element` - Map with `name`, optional `attrs`, `text`, and `children`
* `indent` - Indentation spaces (default: 2, range: 0-8, 0 = compact)

Example:
```cs
let elem = {
  "name": "book",
  "attrs": {"isbn": "123"},
  "children": [
    {"name": "title", "text": "Guide"},
    {"name": "author", "text": "John"}
  ]
};
let xml = xml_stringify(elem);
// Returns:
// <book isbn="123">
//   <title>Guide</title>
//   <author>John</author>
// </book>
```

## Time

### `now_ms() -> int`

Milliseconds timestamp:

* Uses `gettimeofday` on POSIX
* Uses `clock()` fallback on Windows build

### `sleep(ms: int)`

Returns a promise that resolves after `ms` milliseconds.

```c
await sleep(10);
```

If `ms <= 0`, the promise resolves immediately.

### Background Event Loop (Linux only)

`event_loop_start()` -> `bool`  — Start the background event loop (returns `true` on success).

`event_loop_stop()` -> `bool`   — Stop and join the background event loop.

`event_loop_running()` -> `bool` — Check if the background event loop is running.

Note: This is a Linux-only feature that runs the VM scheduler and pending I/O on a separate thread so the main thread can block on `await` while the event loop makes progress.

## Promises

### `promise() -> promise`

Creates a new pending promise.

### `resolve(promise, value = nil) -> bool`

Resolves a promise. Returns `true` if it was pending.

### `reject(promise, value = nil) -> bool`

Rejects a promise. Returns `true` if it was pending.

### `is_promise(value) -> bool`

Checks if a value is a promise.

## Math Functions

CupidScript provides a comprehensive set of mathematical functions for scientific computing, game development, and general numeric operations.

### Basic Math

#### `abs(x) -> int | float`

Returns absolute value. Preserves type (int → int, float → float).

```cs
abs(-5)      // 5
abs(-3.14)   // 3.14
abs(42)      // 42
```

#### `min(...values) -> number`

Returns minimum of one or more values. Accepts any number of int and/or float arguments.

```cs
min(3, 7, 2)      // 2
min(1.5, 2.5)     // 1.5
min(-10, 0, 10)   // -10
```

#### `max(...values) -> number`

Returns maximum of one or more values. Accepts any number of int and/or float arguments.

```cs
max(3, 7, 2)      // 7
max(1.5, 2.5)     // 2.5
max(-10, 0, 10)   // 10
```

#### `clamp(value, min, max) -> number`

Constrains a value to the range [min, max]. Returns int if all arguments are int, otherwise float.

```cs
clamp(5, 0, 10)    // 5
clamp(-5, 0, 10)   // 0
clamp(15, 0, 10)   // 10
clamp(3.7, 0, 5)   // 3.7
```

### Rounding Functions

#### `floor(x) -> int`

Rounds down to nearest integer. Accepts int or float, always returns int.

```cs
floor(3.7)    // 3
floor(-3.7)   // -4
floor(5)      // 5
```

#### `ceil(x) -> int`

Rounds up to nearest integer. Accepts int or float, always returns int.

```cs
ceil(3.2)     // 4
ceil(-3.2)    // -3
ceil(5)       // 5
```

#### `round(x) -> int`

Rounds to nearest integer (0.5 rounds away from zero). Returns int.

```cs
round(3.7)    // 4
round(3.2)    // 3
round(3.5)    // 4
round(-3.5)   // -4
```

#### `trunc(x) -> int`

Truncates decimal part (rounds toward zero). Returns int.

```cs
trunc(3.7)    // 3
trunc(-3.7)   // -3
trunc(5)      // 5
```

### Powers and Roots

#### `sqrt(x) -> float`

Returns square root. Accepts int or float, always returns float.

```cs
sqrt(16)      // 4.0
sqrt(2)       // 1.414...
sqrt(0)       // 0.0
```

#### `pow(base, exp) -> float`

Returns `base` raised to `exp` power. Accepts int or float, returns float.

```cs
pow(2, 8)     // 256.0
pow(10, 3)    // 1000.0
pow(2, -1)    // 0.5
pow(4, 0.5)   // 2.0 (square root)
```

#### `exp(x) -> float`

Returns $e^x$ (Euler's number raised to power x).

```cs
exp(0)        // 1.0
exp(1)        // 2.71828... (E)
exp(2)        // 7.389...
```

### Logarithms

#### `log(x) -> float`

Returns natural logarithm of `x` (base $e$).

```cs
log(E)        // 1.0
log(1)        // 0.0
log(10)       // 2.302...
```

#### `log10(x) -> float`

Returns base-10 logarithm of `x`.

```cs
log10(100)    // 2.0
log10(1000)   // 3.0
log10(1)      // 0.0
```

#### `log2(x) -> float`

Returns base-2 logarithm of `x`.

```cs
log2(8)       // 3.0
log2(1024)    // 10.0
log2(1)       // 0.0
```

### Trigonometry

All trigonometric functions use **radians**. Use `PI` constant for conversions.

#### `sin(x) -> float`

Returns sine of `x` (radians).

```cs
sin(0)           // 0.0
sin(PI / 2)      // 1.0
sin(PI)          // ~0.0
```

#### `cos(x) -> float`

Returns cosine of `x` (radians).

```cs
cos(0)           // 1.0
cos(PI / 2)      // ~0.0
cos(PI)          // -1.0
```

#### `tan(x) -> float`

Returns tangent of `x` (radians).

```cs
tan(0)           // 0.0
tan(PI / 4)      // 1.0
```

#### `asin(x) -> float`

Returns arc sine of `x` (result in radians). Input must be in range [-1, 1].

```cs
asin(0)          // 0.0
asin(1)          // 1.5707... (PI/2)
asin(0.5)        // 0.5235... (PI/6)
```

#### `acos(x) -> float`

Returns arc cosine of `x` (result in radians). Input must be in range [-1, 1].

```cs
acos(1)          // 0.0
acos(0)          // 1.5707... (PI/2)
acos(-1)         // 3.1415... (PI)
```

#### `atan(x) -> float`

Returns arc tangent of `x` (result in radians).

```cs
atan(0)          // 0.0
atan(1)          // 0.7853... (PI/4)
atan(-1)         // -0.7853... (-PI/4)
```

#### `atan2(y, x) -> float`

Returns arc tangent of `y/x` using signs of both arguments to determine quadrant (result in radians, range [-π, π]).

```cs
atan2(1, 1)      // 0.7853... (PI/4, 45°)
atan2(1, 0)      // 1.5707... (PI/2, 90°)
atan2(0, -1)     // 3.1415... (PI, 180°)
atan2(-1, 0)     // -1.5707... (-PI/2, -90°)
```

**Use Case:** Converting Cartesian coordinates to polar angle:

```cs
let angle = atan2(y, x);  // Correctly handles all quadrants
```

### Random Functions

All random functions automatically seed on first use.

#### `random() -> float`

Returns a pseudo-random float in the range [0.0, 1.0).

```cs
let r = random();        // e.g., 0.7234...
let percent = random() * 100;  // 0 to 100
```

#### `random_int(min, max) -> int`

Returns a random integer in the range [min, max] (inclusive).

```cs
random_int(1, 6)         // Dice roll: 1, 2, 3, 4, 5, or 6
random_int(0, 100)       // Random percentage
random_int(-10, 10)      // Random value from -10 to 10
```

**Note:** Arguments can be in any order (automatically swapped if needed).

#### `random_choice(list) -> value`

Returns a random element from a list. Returns `nil` if list is empty.

```cs
let colors = ["red", "green", "blue"];
let color = random_choice(colors);  // e.g., "green"

let options = [1, 2, 3, 4, 5];
let pick = random_choice(options);  // e.g., 3
```

#### `shuffle(list) -> nil`

Randomly shuffles a list in-place using Fisher-Yates algorithm. Returns `nil`.

```cs
let deck = [1, 2, 3, 4, 5];
shuffle(deck);
print(deck);  // e.g., [3, 1, 5, 2, 4]
```

**Use Case:** Randomize order for games, simulations, or sampling.

### Degree/Radian Conversion

```cs
// Degrees to radians
fn deg_to_rad(deg) { return deg * PI / 180; }

// Radians to degrees
fn rad_to_deg(rad) { return rad * 180 / PI; }

// Examples
let angle_rad = deg_to_rad(90);   // PI/2
let angle_deg = rad_to_deg(PI);   // 180
```

## Math Constants

### `PI`

The constant π ≈ 3.14159265358979323846.

```cs
let circumference = 2 * PI * radius;
let area = PI * radius * radius;
```

### `E`

Euler's number $e$ ≈ 2.71828182845904523536.

```cs
let growth = E;
let decay = exp(-t / tau);
```

### Complete Example

```cs
// Calculate distance between two points
fn distance(x1, y1, x2, y2) {
    let dx = x2 - x1;
    let dy = y2 - y1;
    return sqrt(dx * dx + dy * dy);
}

// Clamp value with bounds checking
fn safe_clamp(val, minimum, maximum) {
    return clamp(val, min(minimum, maximum), max(minimum, maximum));
}

// Random point on unit circle
fn random_unit_circle() {
    let angle = random() * 2 * PI;
    return {x: cos(angle), y: sin(angle)};
}

// Test math functions
print("Distance:", distance(0, 0, 3, 4));  // 5.0
print("Clamped:", safe_clamp(15, 0, 10));  // 10
let point = random_unit_circle();
print("Random point:", point.x, point.y);
```

## Conversion

### `to_int(value) -> int | nil`

Converts a value to integer:

* `int` → returns as-is
* `float` → truncates toward zero (3.9 → 3, -3.9 → -3)
* `bool` → `0` or `1`
* `string` → parses decimal integer, returns `nil` if invalid
* Otherwise → `nil`

### `to_str(value) -> string`

Converts any value to its string representation.

## Memory Management

### `gc() -> int`

Manually triggers garbage collection cycle detection. Returns the number of cycles collected.

### `gc_stats() -> map`

Returns GC statistics as a map:

* `tracked` - Number of tracked objects (lists/maps)
* `collections` - Total collections performed
* `collected` - Total objects collected
* `allocations` - Total allocations since VM start

### `gc_config() -> map`

Returns current GC configuration:

* `threshold` - Collection threshold (0 = disabled)
* `alloc_trigger` - Allocation trigger interval (0 = disabled)

### `gc_config(map) -> bool`

Sets GC configuration from a map with optional fields:

* `threshold` (int)
* `alloc_trigger` (int)

Returns `true`.

### `gc_config(threshold, alloc_trigger) -> bool`

Sets both values directly. Returns `true`.

Auto-GC Policy:

* GC can run when tracked objects >= `threshold`
* GC can run every `alloc_trigger` allocations
* Both policies can be enabled simultaneously
* Setting either value to `0` disables that policy

See [Implementation Notes](Implementation-Notes#garbage-collection) for details.

## Safety Controls

Scripts can configure their own execution limits:

### `set_timeout(milliseconds)`

Sets maximum execution time in milliseconds. Script aborts if exceeded.

```c
set_timeout(30000);  // Allow 30 seconds for batch processing
```

### `set_instruction_limit(count)`

Sets maximum instruction count. Script aborts if exceeded.

```c
set_instruction_limit(100000000);  // Allow 100M instructions
```

### `get_timeout() -> int`

Returns current timeout in milliseconds (0 = unlimited).

### `get_instruction_limit() -> int`

Returns current instruction limit (0 = unlimited).

### `get_instruction_count() -> int`

Returns number of instructions executed in current script run.

**Example:**

```c
// Check if we need more time for heavy processing
if (get_instruction_count() > get_instruction_limit() / 2) {
    print("Warning: approaching instruction limit");
    set_instruction_limit(get_instruction_limit() * 2);
}
```

## Network I/O

### TCP Sockets

```c
// Connect to a server
let sock = await tcp_connect("example.com", 80);

// Send data
await socket_send(sock, "GET / HTTP/1.1\r\nHost: example.com\r\n\r\n");

// Receive data
let response = await socket_recv(sock, 4096);

// Close socket
socket_close(sock);
```

### TCP Server

```c
let server = tcp_listen("0.0.0.0", 8080);
print("Listening on port 8080...");

let client = await socket_accept(server);
let data = await socket_recv(client, 1024);
await socket_send(client, "HTTP/1.1 200 OK\r\n\r\nHello!");
socket_close(client);
socket_close(server);
```

### TLS/SSL Connections

> **Note:** TLS support requires building without `CS_NO_TLS` flag and linking against OpenSSL/LibreSSL.

```c
// Connect with TLS (requires TLS support compiled in)
let sock = await tls_connect("example.com", 443);

// Check if secure
print(socket_is_secure(sock));  // true

// Get TLS info
let info = tls_info(sock);
print(info.version);   // "TLSv1.3"
print(info.cipher);    // "TLS_AES_256_GCM_SHA384"

// Upgrade existing connection (STARTTLS)
let smtp = await tcp_connect("smtp.example.com", 587);
await socket_send(smtp, "STARTTLS\r\n");
await tls_upgrade(smtp);  // Now encrypted

socket_close(sock);
```

### HTTP/HTTPS Client

```c
// HTTP request
let resp = await http_get("http://example.com");

// HTTPS request (automatic TLS, requires TLS support)
let resp = await http_get("https://api.example.com/data");
print(resp.status);  // 200
print(resp.body);

// POST request
let resp = await http_post("https://api.example.com/data", json_stringify({key: "value"}));

// DELETE request
let resp = await http_delete("https://api.example.com/resource/1");

// Full control with http_request
let resp = await http_request({
  method: "PUT",
  url: "https://api.example.com/users/1",
  headers: {
    "Content-Type": "application/json",
    "Authorization": "Bearer token123"
  },
  body: json_stringify({name: "bob"}),
  timeout: 10000,
  follow_redirects: true,
  verify_ssl: true  // default, set false to skip cert verification
});
```

**Note:** All HTTP functions are asynchronous and return promises. Use `await` to get the response.

### URL Utilities

```c
let parts = url_parse("http://user:pass@example.com:8080/path?q=1");
// {scheme: "http", user: "user", pass: "pass", host: "example.com", port: 8080, path: "/path", query: "q=1"}

let url = url_build({scheme: "https", host: "api.example.com", path: "/v1/users"});
// "https://api.example.com/v1/users"
```

### Configuration

```c
// Set default timeout for network operations (in milliseconds)
net_set_default_timeout(60000);  // 60 second default timeout
```

**Platform Notes:**
- Network I/O uses non-blocking sockets with the event loop
- On Linux/Unix: Uses `poll()` for multiplexing
- On Windows: Uses `select()` for multiplexing
- TLS support is optional and requires OpenSSL/LibreSSL at build time
