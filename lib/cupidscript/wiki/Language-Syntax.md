# Language Syntax

## Table of Contents

- [Comments](#comments)
- [Tokens](#tokens)
- [List & Map Literals and Comprehensions](#list-literals)
- [Set Literals and Comprehensions](#set-literals)
- [Functions](#functions)
- [Arrow Functions](#arrow-functions)
- [Spread & Rest](#spread--rest)
- [Pipe Operator](#pipe-operator)
- [Classes](#classes)
- [Structs](#structs)
- [Enums](#enums)
- [Generators (`yield`)](#generators-yield)
- [Async / Await](#async--await)
- [Operators & Punctuation](#operators--punctuation)
- [Precedence (high to low)](#precedence-high-to-low)
- [`switch` Statement](#switch-statement)
- [`defer` Statement](#defer-statement)
- [`match` Expression](#match-expression)
- [`try` / `catch` / `finally` Statements](#try--catch--finally-statements)
- [`export` Statements](#export-statements)
- [`import` Statements](#import-statements)
- [`const` Bindings](#const-bindings)
- [Destructuring `let`](#destructuring-let)

CupidScript uses a C-like surface syntax.

## Comments

Supported comment forms:

```c
// line comment

/* block comment */
```

Comments are skipped by the lexer alongside whitespace.

## Tokens

### Identifiers

* Start: letter or `_`
* Continue: letters, digits, `_`

### Integers

Decimal and hexadecimal integers with optional underscores:

```c
0
12345
1_000_000
0xFF
0xFF_FF_FF
```

* Decimal: `0-9` digits
* Hexadecimal: `0x` or `0X` followed by hex digits (`0-9`, `a-f`, `A-F`)
* Underscores `_` can appear anywhere in the number (ignored)

### Floats

Floating-point literals with decimal point:

```c
3.14
2.5
0.001
1.5e-3
2.0E+10
```

* Must contain a decimal point: `.`
* Optional exponent: `e` or `E` followed by optional `+`/`-` and digits
* Scientific notation: `1.5e-3` equals `0.0015`

### Strings

Double-quoted strings:

```c
"hello"
"with \n escapes"
"quote: \""
"backslash: \\""
```

Raw (backtick) strings:

```c
`C:\\temp\\file`
`line1
line2`
```

Notes:

* Backtick strings do not process escapes.
* Backtick strings can span multiple lines.
* String interpolation is not supported inside backtick strings.
* A backtick cannot appear inside a backtick string.

String interpolation is supported with `${...}` inside a string:

```c
let name = "Ada";
print("Hello ${name}");
print("sum=${1 + 2}");
```

Escape handling (in runtime unescape):

* `\n`, `\t`, `\r`, `\"`, `\\`
* `\e` (ASCII ESC, 27)
* `\xHH` (single byte from two hex digits)
* Any unknown escape `\x` becomes literal `x`

### List Literals

```c
[]
[1, 2, 3]
["a", "b", "c"]
```

Trailing commas are allowed:

```c
[1, 2, 3,]
[]
```

**List Comprehensions:**

```c
[x * x for x in range(10)]
[x for x in items if x > 0]
```

See [Comprehensions](COMPREHENSIONS) for details.

### Map Literals

```c
{}
{"name": "Frank", "age": 30}
{"key": value_expr}
```

Trailing commas are allowed:

```c
{"a": 1, "b": 2,}
{}
```

**Map Comprehensions:**

```c
{k: v * 2 for k, v in data}
{i: i * i for i in range(5)}
```

See [Comprehensions](COMPREHENSIONS) for details.

Keys are expressions (any value is allowed). For convenience, an identifier before `:` is treated as a string key.

### Set Literals

```c
#{}
#{1, 2, 3}
#{"apple", "banana", "cherry"}
```

Trailing commas are allowed:

```c
#{1, 2, 3,}
#{}
```

**Set Comprehensions:**

```c
#{x * x for x in range(10)}
#{x for x in items if x > 0}
```

**Spread in Set Literals:**

```c
let a = #{1, 2, 3};
let b = #{0, ...a, 4};        // #{0, 1, 2, 3, 4}
let list = [1, 2, 2, 3];
let from_list = #{...list};   // #{1, 2, 3}
```

**Set Operators:**

```c
let a = #{1, 2, 3};
let b = #{2, 3, 4};

a | b   // union: #{1, 2, 3, 4}
a & b   // intersection: #{2, 3}
a - b   // difference: #{1}
a ^ b   // symmetric difference: #{1, 4}
```

See [Collections](Collections#sets) for complete set documentation.

## Functions

Named functions:

```c
fn add(a, b = 1) {
  return a + b;
}
```

Function literals:

```c
let inc = fn(x, step = 1) { return x + step; };
```

Defaults use `=` in the parameter list. Required parameters must come before default parameters. Missing arguments use their default values.

## Arrow Functions

Arrow syntax supports concise single-expression functions:

```c
let double = fn(x) => x * 2;
let add = fn(a, b) => a + b;
```

Block-bodied arrows require explicit `return`:

```c
let f = fn(x) => { return x + 1; };
```

## Spread & Rest

Spread (`...`) expands lists and maps (can appear at any position, including first):

```c
let a = [1, 2, 3];
let b = [0, ...a, 4];       // [0, 1, 2, 3, 4]
let c = [...a, 5];          // [1, 2, 3, 5] - spread at first position

let defaults = {theme: "dark", size: 12};
let config = {...defaults, size: 14};     // {theme: "dark", size: 14}
let merged = {...defaults, ...config};    // spread at first position
```

Rest parameters collect remaining arguments into a list:

```c
fn log_all(prefix, ...items) {
  for item in items { print(prefix, item); }
}
```

## Pipe Operator

The pipe operator passes the left value into the right call.

```c
fn add(a, b) { return a + b; }

let r1 = 10 |> add(5);     // add(10, 5)
let r2 = 10 |> add(5, _);  // add(5, 10)
```

If `_` is present, it marks where the left value should go. Otherwise, the left value becomes the first argument.

## Classes

Class declarations define constructors and methods with single inheritance.

```c
class File {
  fn new(path) { self.path = path; }
  fn is_hidden() { return starts_with(self.path, "."); }
}

class ImageFile : File {
  fn new(path) { super.new(path); self.is_img = ends_with(path, ".png"); }
  fn is_image() { return self.is_img; }
}

let f = File(".secret");
let img = ImageFile("cat.png");
```

* `self` refers to the instance inside methods.
* `super` refers to the parent class inside methods.
* Instances are created by calling the class as a function.

## Structs

Structs define fixed-field data types with positional construction.

```c
struct Point { x, y = 0 }
let p = Point(3);    // x=3, y=0
let q = Point(1, 2); // x=1, y=2
```

## Enums

Enums define named integer constants:

```c
enum Color { Red, Green = 5, Blue }
print(Color.Red);   // 0
print(Color.Green); // 5
print(Color.Blue);  // 6
```

## Generators (`yield`)

Functions that use `yield` return a list of yielded values when called:

```c
fn range(n) {
  let i = 0;
  while (i < n) {
    yield i;
    i += 1;
  }
}

let xs = range(4); // [0, 1, 2, 3]
```

## Async / Await

Async functions and `await` are supported (currently synchronous execution):

```c
async fn add(a, b) { return a + b; }
let v = await add(2, 3);
```

## Operators & Punctuation

### Punctuation

`(` `)` `[` `]` `{` `}` `,` `;` `.`

### Operators

Assignment:

* `=`
* `+=`, `-=`, `*=`, `/=` (compound assignment, works on variables, fields, and index expressions)
* `:=` (walrus operator - assigns and returns value in expressions)

Arithmetic:

* `+ - * / %`

Unary:

* `! -`

Comparison:

* `== != < <= > >=`

Boolean:

* `&& ||`

Bitwise / Set Operations:

* `&` (bitwise AND / set intersection)
* `|` (bitwise OR / set union)
* `^` (bitwise XOR / set symmetric difference)

Note: These operators work on sets when both operands are sets, otherwise they are reserved for future bitwise operations on integers.

Nullish coalescing:

* `??` (returns left operand if not `nil`, otherwise right)

Range:

* `..` (exclusive range, e.g., `0..5` → `[0,1,2,3,4]`)
* `..=` (inclusive range, e.g., `0..=5` → `[0,1,2,3,4,5]`)

### Walrus Operator

The walrus operator (`:=`) assigns a value to a variable and returns that value, allowing assignment within expressions:

```c
// Assignment in conditionals
if (x := get_value()) {
    print("Got value:", x);  // x is available here
}

// Assignment in while loops
while (line := read_line()) {
    process(line);
}

// Assignment in function arguments
result := process(data := fetch_data());

// Chain assignments
print(y := (x := 10) + 5);  // x=10, y=15
```

The walrus operator is useful for:
- **Avoiding redundant function calls**: Assign and test in one expression
- **Cleaner conditionals**: Combine assignment with condition checking
- **Loop conditions**: Assign and check loop variables simultaneously

Rules:
* Left side must be a simple identifier (not a field or index expression)
* Returns the assigned value
* Can be used anywhere an expression is valid
* Precedence is lower than most operators but higher than ternary

Example patterns:

```c
// Read-process-check pattern
while (data := fetch_next()) {
    if (result := process(data)) {
        save(result);
    }
}

// Avoid double computation
if (value := expensive_computation()) {
    use(value);  // value already computed and stored
}

// Nested conditions
if (user := find_user(id)) {
    if (perms := get_permissions(user)) {
        check_access(perms);
    }
}
```

## Precedence (high to low)

1. Primary: literals, identifiers, parenthesized `(expr)`, list literals `[...]`, map literals `{...}`, set literals `#{...}`
2. Postfix: calls `f(...)`, indexing `a[b]`, field access `a.b` (field access works after any expression, e.g. `(expr).field`)
3. Unary: `!expr`, `-expr`
4. Multiplicative: `* / %`
5. Additive: `+ -`
6. Range: `start..end`, `start..=end`
7. Comparison: `< <= > >=`
8. Equality: `== !=`
9. Bitwise AND / Set Intersection: `&`
10. Bitwise XOR / Set Symmetric Difference: `^`
11. Bitwise OR / Set Union: `|`
12. Logical AND: `&&`
13. Logical OR: `||`
14. Nullish: `a ?? b`
15. Pipe: `value |> f()`
16. Ternary: `cond ? then : else`
17. Python-style conditional (in comprehensions): `value_if_true if cond else value_if_false`

> **Note:** The Python-style `if-else` syntax (#17) is primarily supported in comprehension expressions. For general use, prefer the traditional ternary operator (#16).

### Optional chaining

Optional chaining accesses a field only if the target is not `nil`:

```c
user?.name
```

If the target is `nil`, the result is `nil` (no error). Otherwise, it behaves like normal field access.

## `switch` Statement

CupidScript supports `switch` as a statement:

```c
switch (x) {
  case 1 { print("one"); }
  case 2 { print("two"); }
  default { print("other"); }
}
```

Notes:

* Cases are compared using the same equality rules as `==` for basic types.
* Fallthrough: once a case matches, execution continues into subsequent cases until a `break` is hit or the switch ends.
* `default` can appear anywhere; if no case matches, execution starts at `default` and falls through to later cases.

Pattern cases are supported (no guards):

```c
switch (value) {
  case [a, b, ...rest] { print(a, b, rest); }
  case {x, y: yy} { print(x + yy); }
  case int(x) { print("int", x); }
  default { print("other"); }
}
```

## `defer` Statement

Schedule cleanup work to run when the current block exits:

```c
defer close(file);
defer { print("cleanup"); }
```

Notes:

* Defers run in LIFO order when the block exits.
* Defers run even if the block exits via `return`, `break`, `continue`, or `throw`.

## `match` Expression

`match` is like `switch`, but returns a value and is used inside expressions.

```c
let label = match (x) {
  case 1: "one";
  case 2: "two";
  default: "other";
};
```

Patterns and guards:

```c
let label = match (value) {
  case [a, b]: a + b;
  case {x, y: z}: x + z;
  case n if n > 10: "big";
  case _: "small";
};
```

Notes:

* Patterns can bind identifiers; bindings are visible in the guard and case result.
* Guards (`if expr`) are optional and run only when the pattern matches.
* The first matching case expression is evaluated and returned.
* `default` is optional; if omitted and no case matches, the result is `nil`.

## `try` / `catch` / `finally` Statements

Syntax:

```c
try {
  // body
} catch (e) {
  // handler
} finally {
  // cleanup
}

// finally can also be a single expression:
try { work(); } catch (e) { print(e); } finally cleanup();
```

Notes:

* `catch` is required; `finally` is optional.
* `finally` executes after the `try`/`catch` regardless of `return` or `throw`.
* `finally` can be a block or a single expression statement.

## `export` Statements

Syntax:

```c
export name = expr;
export {name, local as exported};
```

Defines a value in the module's implicit `exports` map. `require()` returns this map.

## `import` Statements

Syntax:

```c
import "./path/to/module.cs";           // side effects only
import mod from "./path/to/module.cs";  // bind exports map
import {foo, bar as baz} from "./path/to/module.cs";
import mod, {foo} from "./path/to/module.cs";
```

Notes:

* `import` is syntax sugar over `require()`.
* Named imports read fields from the module's exports map.
* If an imported name is missing, the binding is set to `nil`.

## `const` Bindings

Use `const` to create an immutable binding:

```c
const x = 10;
```

Notes:

* Reassignment to a `const` binding raises a runtime error.
* `const` requires an initializer.
* Destructuring also works with `const`:

```c
const [a, b] = [1, 2];
const {name, age} = {"name": "Ada", "age": 36};
```

## Destructuring `let`

List and map destructuring are supported in `let` declarations.

### List destructuring

```c
let [a, b, c] = [1, 2];
// a=1, b=2, c=nil
```

### Map destructuring

```c
let {x, y} = {"x": 10, "y": 20};
// x=10, y=20

let {key: alias} = {"key": "value"};
// alias="value"
```

Notes:

* Missing list elements or map keys produce `nil`.
* `_` can be used to ignore a binding.
* Destructuring requires an initializer.
* List destructuring expects a list; map destructuring expects a map (runtime error otherwise).
