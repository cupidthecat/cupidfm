# Control Flow

## Table of Contents

- [Blocks](#blocks)
- [`if` / `else`](#if--else)
- [`while`](#while)
- [`for...in`](#forin)
- [C-Style `for` Loops](#c-style-for-loops)
- [Range Operator](#range-operator)
- [`return`](#return)
- [`break` / `continue`](#break--continue)
- [`switch`](#switch)
- [`defer`](#defer)
- [`match` Expression](#match-expression)
- [`try` / `catch` / `finally`](#try--catch--finally)
- [Ternary Expression](#ternary-expression)
- [Exception Handling](#exception-handling)

## Blocks

Blocks are `{ ... }` and appear in:

* function bodies
* `if` / `else`
* `while`

A block contains a sequence of statements.

## `if` / `else`

Syntax:

```c
if (cond) { 
  // then
} else {
  // else
}
```

Notes:

* Parentheses are required: `if ( ... )`
* `else` block is optional

**Walrus operator pattern:**
```c
// Assign and test in one expression
if (result := compute_value()) {
    print("Success:", result);
} else {
    print("Failed");
}

// Nested with walrus
if (user := find_user(id)) {
    if (token := generate_token(user)) {
        send_email(user, token);
    }
}
```

## `while`

Syntax:

```c
while (cond) {
  // body
}
```

Notes:

* Parentheses are required: `while ( ... )`
* Condition is re-evaluated each iteration

**Walrus operator pattern:**
```c
// Read lines until EOF
while (line := read_line()) {
    process(line);
}

// Fetch and process data
while (data := fetch_next()) {
    if (is_valid(data)) {
        handle(data);
    }
}
```

## `for...in`

Syntax:

```c
for name in iterable {
  // body
}
```

Iterates over:

* Lists: iterates over elements
* Maps: iterates over keys (use `keys(m)`, `values(m)`, or `items(m)`)

**IMPORTANT:** The `for k, v in map` syntax **only works in comprehensions**, NOT in regular `for` loops.
Use `items(map)` for key-value iteration in regular loops.

Example:

```c
let xs = [10, 20, 30];
for x in xs {
  print(x);
}

// Using range() for numeric iteration
for i in range(10) {
  print(i); // 0 through 9
}

for i in range(2, 8, 2) {
  print(i); // 2, 4, 6
}

// Map iteration: use items() for key-value pairs
let m = {"a": 1, "b": 2};
for pair in items(m) {
  print(pair[0], "=", pair[1]);
}

// ❌ WRONG: for k, v in m {...} does NOT work in regular loops
// ✅ RIGHT: Use items() as shown above
// ✅ ALSO WORKS: for k, v in m {...} in comprehensions only
```

### List Iteration with Index

**\u26a0\ufe0f Understanding Variable Order:**

When iterating lists with two variables, the order is **(value, index)** - opposite to most languages!

```c
let fruits = ["apple", "banana", "cherry"];

// \u26a0\ufe0f Native syntax: (value, index) - COUNTER-INTUITIVE
for fruit, idx in fruits {
    print(idx, ":", fruit);
    // 0 : apple
    // 1 : banana
    // 2 : cherry
}
```

**RECOMMENDED: Use `enumerate()` for standard (index, value) order:**

```c
// ✅ enumerate() returns [[index, value], ...] pairs
// In regular for loops, access pair elements by index
for pair in enumerate(fruits) {
    let idx = pair[0];
    let fruit = pair[1];
    print(idx, ":", fruit);
    // 0 : apple
    // 1 : banana
    // 2 : cherry
}

// In comprehensions, you can use destructuring syntax
let labeled = [to_str(i) + ": " + val for [i, val] in enumerate([10, 20, 30])];
// ["0: 10", "1: 20", "2: 30"]
```

> **Note:** Destructuring syntax `for [a, b] in ...` only works in **comprehensions**, not in regular `for` loops.

**Best Practice:** Always use `enumerate()` when you need both index and value for clarity and compatibility with other languages.

## C-Style `for` Loops

Syntax:

```c
for (init; condition; increment) {
  // body
}
```

All three parts are optional. This enables traditional C-style loops:

```c
// Basic counter loop
for (i = 0; i < 10; i = i + 1) {
  print(i);
}

// `let` is allowed in the init clause
for (let i = 0; i < 3; i += 1) {
  print(i);
}

// Compound assignments are supported in the header
for (let j = 5; j > 0; j -= 1) {
  print(j);
}

// Multiple initializations (using comma isn't supported, use separate statements)
let i = 0;
let j = 10;
for (; i < j; i = i + 1) {
  print(i, j);
  j = j - 1;
}

// Infinite loop
for (;;) {
  if (some_condition()) {
    break;
  }
}
```

## Range Operator

Syntax:

* `start..end` - Exclusive range (up to but not including end)
* `start..=end` - Inclusive range (up to and including end)

Creates a lazy range from `start` to `end`:

```c
let nums = 0..5;        // range 0..5 (exclusive)
let incl = 0..=5;       // range 0..=5 (inclusive)
let down = 10..5;       // descending range
let down_incl = 10..=5; // descending inclusive range

// Use with for-in loops
for i in 0..10 {
  print(i); // 0 through 9
}

for i in 1..=100 {
  print(i); // 1 through 100
}
```

The range operator returns a lazy range object and works in both directions (ascending and descending) automatically.

## `return`

Syntax:

```c
return;
return expr;
```

Return can appear inside any block; it returns from the current function call.

## `break` / `continue`

* `break;` exits the innermost loop or `switch`
* `continue;` skips to the next iteration

Valid in `while`, `for...in`, and C-style `for` loops.

Example:

```c
while (true) {
  let x = pop(xs);
  if (x == nil) { break; }
  if (x < 0) { continue; }
  print(x);
}
```

## `switch`

Syntax:

```c
switch (expr) {
  case 1 { print("one"); }
  case 2 { print("two"); }
  default { print("other"); }
}
```

Rules:

* `switch` evaluates `expr` once, then compares against each `case` value in order.
* Supported matching is easiest/most common with `int`, `string`, `bool`, and `nil`.
* **Fallthrough:** once a case matches, execution continues into subsequent cases until a `break` is hit or the switch ends.
* `break;` exits the `switch` (without leaving an outer loop).
* `default` (optional) can appear anywhere. If no case matches, execution starts at `default` and falls through to the following cases.

Pattern cases (no guards) are supported:

* List destructuring: `case [a, b, ...rest] { ... }`
* Map destructuring: `case {x, y: yy} { ... }`
* Type pattern: `case int(x) { ... }`

Type patterns match built-in types (`nil`, `bool`, `int`, `float`, `string`, `list`, `map`, `strbuf`, `range`, `function`, `native`, `promise`).
If a class or struct name is in scope, `case ClassName(x)` or `case StructName(x)` matches instances and binds `x`.

Example:

```c
let out = [];
switch (x) {
  case 1 { push(out, "one"); }
  case 2 { push(out, "two"); break; }
  default { push(out, "other"); }
}
```

## `defer`

The `defer` statement schedules cleanup code to run when the current block exits.

Syntax:

```c
defer expr;
defer { /* block */ }
```

Rules:

* Defers run in **last-in, first-out** order when leaving the current block.
* Defers run on normal block exit, and also on `return`, `break`, `continue`, or `throw`.
* `defer` is usually used for cleanup like closing files or restoring state.

## `match` Expression

Syntax:

```c
let label = match (x) {
  case 1: "one";
  case 2: "two";
  default: "other";
};
```

Pattern syntax:

```c
let out = match (value) {
  case [a, b]: a + b;              // list pattern (binds a, b)
  case {x, y: z}: x + z;           // map pattern (keys x, y; binds y to z)
  case int(n): n + 1;              // type pattern (binds n)
  case _ : "anything";            // wildcard
  case n if n > 10: "big";        // guard (optional)
  default: "other";
};
```

Rules:

* `match` evaluates the input expression once, then tests each `case` pattern in order.
* Patterns can bind identifiers; bindings are visible in the guard and case result.
* Guards (`if expr`) are optional and only run if the pattern matches.
* The first matching case expression is evaluated and returned.
* `default` (optional) is returned if no case matches.
* If no case matches and there is no `default`, the result is `nil`.

## `try` / `catch` / `finally`

Syntax:

```c
try {
  // work
} catch (e) {
  // error handling
} finally {
  // cleanup (always runs)
}

// finally can also be a single expression:
try {
  work();
} catch (e) {
  print(e);
} finally cleanup();
```

Notes:

* `catch` is required; `finally` is optional.
* The `finally` block runs on normal completion, `return`, `break`/`continue`, or `throw`.
* `finally` can be a block or a single expression statement.
* If `finally` itself returns or throws, it overrides the prior control flow.
* `break;` inside a `switch` exits the switch (not an enclosing loop).

## Ternary Expression

Syntax:

```c
cond ? then_val : else_val
```

Evaluates `cond`. If truthy, returns `then_val`, otherwise `else_val`.

Example:

```c
let max = a > b ? a : b;
```

## Exception Handling

### `throw`

Syntax:

```c
throw expr;
```

Throws an exception. The value can be any type (commonly a string or map).

Example:

```c
if (x < 0) {
  throw "negative value not allowed";
}

throw {"msg": "error", "code": 404};
```

### `try / catch`

Syntax:

```c
try {
  // code that might throw
} catch (e) {
  // handle exception
}
```

If code in the `try` block throws, execution jumps to the `catch` block with the thrown value in variable `e`.

Example:

```c
try {
  do_risky_operation();
} catch (e) {
  print("Error:", e);
}
```
