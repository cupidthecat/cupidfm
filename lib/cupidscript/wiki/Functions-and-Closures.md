# Functions & Closures

## Table of Contents

- [Named Functions: `fn name(...) { ... }`](#named-functions-fn-name)
- [Function Literals: `fn (...) { ... }`](#function-literals-fn)
- [Calls](#calls)
- [Default Parameters](#default-parameters)
- [Arrow Functions](#arrow-functions)
- [Rest Parameters](#rest-parameters)
- [Closures Example](#closures-example)

CupidScript supports **named functions** and **function literals**.

## Named Functions: `fn name(...) { ... }`

Syntax:

```c
fn add(a, b) {
  return a + b;
}
```

* Functions are values (callable)
* Parameters are identifiers
* The body is a block `{ ... }`

## Function Literals: `fn (...) { ... }`

Syntax:

```c
let f = fn(x) { return x + 1; };
print(f(10));
```

Function literals capture the current environment (closure). This is implemented by storing `closure = env` when the function value is created.

## Calls

Syntax:

```c
f();
f(1, 2, 3);
```

Argument count must be between the required and total parameter counts. Missing arguments use default values when provided. Too few or too many arguments is a runtime error.

## Default Parameters

Default values are allowed in function definitions and function literals:

```c
fn greet(name, greeting = "Hello") {
  return greeting + ", " + name + "!";
}

let add = fn(a, b = 10) { return a + b; };
```

Rules:

* Required parameters must come before default parameters.
* Defaults are evaluated at call time in the callee’s closure.
* Defaults can reference earlier parameters.
* Passing `nil` explicitly uses `nil` (it does not trigger the default).

## Arrow Functions

Arrow functions provide a concise syntax with optional implicit return.

```c
let add = fn(a, b) => a + b;
let inc = fn(x) => x + 1;
let block = fn(x) => { return x * 2; };
```

Rules:

* `fn(args) => expr` returns the expression value.
* `fn(args) => { ... }` uses a block body; explicit `return` is required.
* Named functions can also use `=>`.

## Rest Parameters

Rest parameters collect extra arguments into a list.

```c
fn log_all(prefix, ...items) {
  for item in items {
    print(prefix, item);
  }
}

log_all(">", "a", "b", "c");
```

Rules:

* Rest parameter must be the last parameter.
* `items` is a list of any remaining arguments (possibly empty).

## Closures Example

```c
fn counter() {
  let i = 0;
  return fn() {
    i = i + 1;
    return i;
  };
}

let c = counter();
print(c()); // 1
print(c()); // 2
```

`i` lives in the captured environment and persists across calls.
