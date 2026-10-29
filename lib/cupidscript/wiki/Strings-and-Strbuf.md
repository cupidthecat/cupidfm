# Strings & strbuf

## Table of Contents

- [Strings](#strings)
- [strbuf (string builder)](#strbuf-string-builder)

## Strings

### Literals

```c
"hello"
"line1\nline2"
```

Raw (backtick) strings:

```c
`C:\\temp\\file`
`line1
line2`
```

Notes:

* Backtick strings do not process escapes (backslashes are literal).
* They can span multiple lines.
* String interpolation is not supported inside backtick strings.
* A backtick cannot appear inside a backtick string.

Escapes supported:

* `\n \t \r \" \\`
* `\e` for ASCII ESC (27)
* `\xHH` for a single byte using two hex digits
* Unknown escapes become literal character (e.g. `\q` → `q`)

### Concatenation with `+`

* `int + int` → arithmetic
* If either side is a string → string concatenation

  * non-string side is rendered as an integer string in the current implementation (best behavior guaranteed for ints; other objects use a generic representation in some contexts)

Example:

```c
print("x=" + 10);
```

### String interpolation

CupidScript supports `${...}` expressions inside double-quoted strings.

```c
let name = "Ada";
let count = 3;
print("Hello ${name}, you have ${count} messages.");
```

Notes:

* Any expression is allowed inside `${...}`.
* Values are converted to strings using the same representation as `to_str()`.
* Escapes still work inside the literal parts of the string.

Examples:

```c
print("x=${1 + 2}");
print("bool=${true}");
print("nil=${nil}");
```

### String helpers (stdlib)

See `Standard-Library.md` for:

* `str_find`
* `str_replace`
* `str_split`
* `str_contains`
* `str_count`
* `str_pad_start`
* `str_pad_end`
* `str_reverse`
* `substr`
* `join`
* `to_str`
* `to_int`

### String Ergonomics

New string manipulation functions for common operations:

#### Trimming

* `str_trim(s)` / `trim(s)` - Remove whitespace from both ends
* `str_ltrim(s)` / `ltrim(s)` - Remove leading whitespace
* `str_rtrim(s)` / `rtrim(s)` - Remove trailing whitespace

```c
let s = "  hello world  ";
print(str_trim(s));    // "hello world"
print(str_ltrim(s));   // "hello world  "
print(str_rtrim(s));   // "  hello world"
```

#### Case Conversion

* `str_lower(s)` / `lower(s)` - Convert to lowercase
* `str_upper(s)` / `upper(s)` - Convert to uppercase

```c
print(str_lower("Hello World"));  // "hello world"
print(str_upper("Hello World"));  // "HELLO WORLD"
```

#### Substring Checks

* `str_startswith(s, prefix)` / `starts_with(s, prefix)` - Check if string starts with prefix
* `str_endswith(s, suffix)` / `ends_with(s, suffix)` - Check if string ends with suffix

```c
print(str_startswith("hello.txt", "hello"));  // true
print(str_endswith("hello.txt", ".txt"));     // true
print(str_startswith("hello.txt", "world"));  // false
```

#### String Utilities

* `str_repeat(s, count)` - Repeat string N times
* `split_lines(s)` - Split into lines on `\n` or `\r\n`
* `str_contains(s, sub)` - Substring check
* `str_count(s, sub)` - Count occurrences
* `str_pad_start(s, width, pad)` - Left pad
* `str_pad_end(s, width, pad)` - Right pad
* `str_reverse(s)` - Reverse bytes

```c
print(str_repeat("abc", 3));  // "abcabcabc"
print(str_repeat("x", 5));    // "xxxxx"
print(split_lines("a\n b\r\n")); // ["a", " b", ""]
print(str_contains("hello", "ell")); // true
print(str_count("banana", "na"));    // 2
print(str_pad_start("7", 4, "0"));   // "0007"
print(str_pad_end("7", 4, "0"));     // "7000"
print(str_reverse("Cupid"));          // "dipuC" (byte-wise)
```

## strbuf (string builder)

Create:

```c
let b = strbuf();
```

Methods are invoked via "field call" syntax:

```c
b.append("hi");
b.append(123);
b.append(true);
b.append(nil);

print(b.str());
print(b.len());
b.clear();
```

### Methods

#### `append(value)`

Appends:

* string contents
* integer as decimal
* bool as `true`/`false`
* nil as `nil`

Anything else may fail (runtime error).

#### `str()`

Returns current contents as an immutable `string`.

#### `clear()`

Resets buffer to empty.

#### `len()`

Returns current length as `int`.
