# CupidScript Wiki

## Table of Contents

- [Quick Example](#quick-example)
- [Language at a Glance](#language-at-a-glance)
- [Wiki Pages](#wiki-pages)

CupidScript is a small embeddable scripting language with:

* A C lexer + recursive-descent parser
* A tree-walking VM (interpreter)
* Closures (function literals capture their environment)
* Basic built-in types: `nil`, `bool`, `int`, `float`, `string`, `list`, `map`, `set`, `strbuf`, `range`, `promise`, `tuple`
* Async/await with promises and a cooperative scheduler
* Pattern matching (`match`) and `switch` with patterns
* A practical standard library for scripts (printing, strings, collections, paths, regex, math, time, JSON)

## Quick Example

```c
// example.cs
fn add(a, b) { return a + b; }

let xs = list();
push(xs, 10);
push(xs, 32);

print("sum:", add(xs[0], xs[1]));
```

## Language at a Glance

### Keywords

`let`, `const`, `fn`, `async`, `await`, `yield`, `class`, `struct`, `enum`, `self`, `super`, `if`, `else`, `while`, `switch`, `case`, `default`, `for`, `in`, `break`, `continue`, `return`, `throw`, `try`, `catch`, `finally`, `defer`, `match`, `import`, `export`, `true`, `false`, `nil`

### Expressions

* Literals: `123`, `0xFF`, `1_000`, `3.14`, `1.5e-3`, `"hello"`, `true`, `false`, `nil`, `[1, 2, 3]`, `{"key": "value"}`, `#{1, 2, 3}`
* Arithmetic: `+ - * / %`
* Comparisons: `< <= > >= == !=`
* Boolean: `&& || !`
* Set operations: `|` (union), `&` (intersection), `-` (difference), `^` (symmetric difference)
* Range: `start..end` (exclusive), `start..=end` (inclusive)
* Ternary: `cond ? then_val : else_val`
* Walrus: `x := expr` (assigns and returns value)
* Calls: `f(a, b)`
* Indexing: `xs[0]`, `m["key"]`
* Field access: `obj.field` (maps) + dotted global fallback
* Function literals: `fn(a, b) { ... }`, `fn(a, b) => expr`
* Defaults: `fn greet(name, greeting = "Hello") { ... }`
* Spread: `[0, ...xs]`, `{...m1, ...m2}`, `#{...set}`, `f(...xs)`
* Pipe: `value |> f()` (supports `_` placeholder)

### Statements

* `let name = expr;`
* `name = expr;` (assign)
* `name += expr;` (compound assign: `+=`, `-=`, `*=`, `/=`)
* `xs[i] = expr;` (setindex)
* `if (cond) { ... } else { ... }`
* `while (cond) { ... }`
* `for name in iterable { ... }` (for-in loops)
* `for (init; cond; incr) { ... }` (C-style for loops)
* `break;` / `continue;`
* `throw expr;`
* `try { ... } catch (e) { ... }`
* `return expr;`
* `expr;` (expression statement)
* `class Name { ... }` (optionally `: Parent`)

## Wiki Pages

* [Getting Started](Getting-Started)
* [Language Syntax](Language-Syntax)
* [Types & Values](Types-and-Values)
* [Control Flow](Control-Flow)
* [Functions & Closures](Functions-and-Closures)
* [Classes & Inheritance](Classes-and-Inheritance)
* [Structs](Structs)
* [Enums](Enums)
* [Tuples](TUPLES)
* [Generators](Generators)
* [Comprehensions](COMPREHENSIONS)
* [Async & Await](Async-Await)
* [Background Event Loop (Linux)](Event-Loop)
* [Collections (list/map/set)](Collections)
* [Strings & strbuf](Strings-and-Strbuf)
* [Regex](Regex)
* [Date & Time](Date-Time)
* [Standard Library](Standard-Library)
* [Modules (load/require) & Paths](Modules-and-Paths)
* [Errors & Stack Traces](Errors-and-Debugging)
* [Host Safety Controls](Host-Safety-Controls)
* [Tests](Tests)
* [Data Formats](Data-Formats)
* [Implementation Notes (Lexer/Parser/VM)](Implementation-Notes)
