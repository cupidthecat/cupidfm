# Types & Values

## Table of Contents

- [Built-in Types](#built-in-types)
- [Mutability](#mutability)
- [Truthiness Rules](#truthiness-rules)
- [Equality](#equality)
- [Comparisons](#comparisons)

CupidScript is dynamically typed.

## Built-in Types

### `nil`

Represents "no value".

* Printed as `nil`
* Falsy in conditionals

### `bool`

`true` / `false`

* Only `false` is falsy (besides `nil`)

### `int`

Signed integer (implemented as 64-bit in the VM: `int64_t`).

### `float`

Floating-point number (implemented as 64-bit double: `double`).

* Float literals: `3.14`, `2.5`, `1.0e-3`
* Arithmetic with mixed int/float returns float
* Division `/` always returns float for precision
* Comparisons work between int and float

### `string`

Heap-allocated refcounted string (`cs_string`).

* `+` concatenates if either operand is a string

### `bytes`

Mutable byte buffer for binary data.

* Indexing: `b[i]` where `i` is int (0..len-1) returns an int 0..255
* Assignment: `b[i] = 255` writes a byte
* Out-of-range or negative index returns `nil`

### `list`

Dynamic array of `cs_value`.

* Indexing: `xs[i]` where `i` is int
* Out-of-range or negative index returns `nil`

### `map`

Simple key/value map with **generalized keys**.

* Keys can be any value (string, int, bool, list, map, etc.)
* Indexing: `m[key]`
* Missing keys return `nil`
* Field access is sugar: `m.key` reads `"key"` from map (string key)

Key equality uses the same rules as `==`:

* `int` and `float` compare by numeric value (so `1` equals `1.0`)
* `string` compares by content
* Other heap objects compare by identity (same object)

### `set`

Unique collection of values (internally a hash set).

* Values are unique by `==`
* Literal syntax: `#{1, 2, 3}` or `#{}` for empty set
* Set comprehensions: `#{x for x in list if condition}`
* Spread operator: `#{...set}`, `#{...list}`
* Methods: `.add(value)`, `.remove(value)`, `.contains(value)`, `.size()`, `.clear()`
* Operators: `|` (union), `&` (intersection), `-` (difference), `^` (symmetric difference)
* Iterable with `for x in set { ... }`
* Order is not guaranteed

See [Collections - Sets](Collections.md#sets) for complete documentation.

### `strbuf`

Mutable string builder object.

* Has method-like calls: `sb.append(x)`, `sb.str()`, `sb.clear()`, `sb.len()`

### `promise`

Promise values represent pending asynchronous results:

* Created by `async` function calls, `sleep(ms)`, or `promise()`
* Resolved or rejected by the scheduler or `resolve`/`reject`
* `await` blocks until a promise resolves and throws on rejection

### `tuple`

**Immutable, fixed-size** value groupings for structured data.

* **Positional tuples**: `(10, 20, 30)` - accessed by index
* **Named tuples**: `(x: 10, y: 20)` - accessed by field name
* **Immutable**: Fields cannot be modified after creation
* **Destructuring**: `let [x, y, z] = coords`
* **Multiple returns**: Return multiple values from functions

```cupidscript
// Positional tuple
let coords = (10, 20, 30)
print(coords[0])  // 10

// Named tuple
let point = (x: 100, y: 200)
print(point.x)    // 100
print(point[0])   // 100 (also works)

// Destructuring
let [a, b, c] = coords

// Function returns
fn get_bounds() {
    return (min: 0, max: 100)
}
```

See [Tuples](TUPLES) for detailed documentation.

## Mutability

* Lists, maps, and sets are mutable.
* Bytes buffers are mutable.
* Strings and **tuples** are immutable.
* `const` creates an immutable binding (reassignment is not allowed), but the value itself may still be mutable (e.g., list/map contents).

### `range`

Lazy range object produced by the `..` / `..=` operators or by `range(...)`.

* Iterable with `for x in range { ... }`
* Does not allocate a list

## Truthiness Rules

Used by `if` and `while`, and by `!`, `&&`, `||`:

* `nil` → false
* `bool` → its value
* everything else → true

That means `0` is truthy (because it's an `int`, not `nil`/`bool`).

## Equality

`==` / `!=` behavior:

* If types differ → not equal
* `nil` equals `nil`
* `bool`, `int`, `string`, `bytes` compare by value
* Other heap objects (including `list`, `map`, `set`, `strbuf`) compare by pointer identity (same object)

## Comparisons

`< <= > >=` only work when:

* both operands are `int` or `float` (can be mixed), or
* both operands are `string`

Otherwise runtime error: comparisons require both numbers or both strings.
