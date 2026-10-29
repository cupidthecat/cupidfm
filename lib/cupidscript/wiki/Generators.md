
# Generators

## Table of Contents

- [Syntax](#syntax)
- [Semantics](#semantics)
- [Errors](#errors)

## Table of Contents

- [Syntax](#syntax)
- [Semantics](#semantics)
- [Errors](#errors)

Generators are functions that use `yield` to produce a sequence of values. Calling a generator executes the function immediately and returns a list of the yielded values.

## Syntax

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

## Semantics

* `yield expr` appends a value to the generator result list.
* A function becomes a generator if it contains any `yield` statement.
* Calling a generator returns a list of all yielded values.
* `return` values in a generator are ignored (the list is returned instead).

## Errors

Using `yield` outside of a generator is a runtime error.
