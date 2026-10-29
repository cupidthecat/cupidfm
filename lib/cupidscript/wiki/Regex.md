# Regex

## Table of Contents

- [Availability](#availability)
- [Pattern Syntax](#pattern-syntax)
- [Functions](#functions)
- [Examples](#examples)
- [Error Handling](#error-handling)

CupidScript exposes regular expressions through the standard library. The engine is POSIX Extended Regular Expressions (ERE).

## Availability

Regex is supported on POSIX builds (Linux/macOS). On Windows builds, regex functions may be unavailable.

## Pattern Syntax

Patterns follow POSIX ERE rules (examples):

* `[0-9]+` - one or more digits
* `[a-z]+` - lowercase letters
* `^...$` - anchors for full-string matches
* `( ... )` - capturing groups

## Functions

### `regex_is_match(pattern, text) -> bool`

Returns `true` if **any** substring matches.

### `regex_match(pattern, text) -> bool`

Returns `true` only if the **entire** string matches. Use `^` and `$` to be explicit.

### `regex_find(pattern, text) -> map | nil`

Returns the first match or `nil` if none. The match object contains:

* `start` - byte offset of match start
* `end` - byte offset of match end
* `match` - the matched substring
* `groups` - list of capture groups (`nil` for unmatched groups)

### `regex_find_all(pattern, text) -> list[map]`

Returns all matches in order. Each entry is the same match object described above.

### `regex_replace(pattern, text, replacement) -> string`

Replaces all matches with a **literal** replacement string. Capture expansions (like `$1`) are not supported.

## Examples

```c
let text = "User: alice42, email: alice@example.com";

print(regex_is_match("[0-9]+", text));
print(regex_match("^User: \\w+$", text));

let email = regex_find("([a-z]+)@([a-z]+\\.[a-z]+)", text);
if (email != nil) {
  print(email["match"]);
  print(email.groups[0]); // user
  print(email.groups[1]); // domain
}

let nums = regex_find_all("[0-9]+", "x=7 y=42 z=105");
print(len(nums));

print(regex_replace("[a-z]+@[a-z]+\\.[a-z]+", text, "<hidden>"));
```

## Error Handling

Invalid patterns raise a runtime error. Wrap in `try/catch` if needed:

```c
try {
  regex_find("(", "text");
} catch (e) {
  print("regex error:", e);
}
```
