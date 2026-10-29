
# Structs

## Table of Contents

- [Syntax](#syntax)
- [Fields and Defaults](#fields-and-defaults)
- [Construction Rules](#construction-rules)
- [Field Access](#field-access)
- [Notes](#notes)

## Table of Contents

- [Syntax](#syntax)
- [Fields and Defaults](#fields-and-defaults)
- [Construction Rules](#construction-rules)
- [Field Access](#field-access)
- [Notes](#notes)

Structs are lightweight, immutable-by-definition *types* with fixed fields and positional construction. They are ideal for small data carriers when you don’t need methods or inheritance.

## Syntax

```c
struct Point { x, y = 0 }

let p = Point(3);      // x=3, y=0
let q = Point(1, 2);   // x=1, y=2
```

## Fields and Defaults

* Fields are declared by name.
* Defaults are optional and use `=`.
* Missing arguments use defaults; if no default is given, the field becomes `nil`.

```c
struct Person { name, age = 0 }
let a = Person("Ada");
let b = Person("Bob", 42);
```

## Construction Rules

* Structs are constructed by **calling** the struct name like a function.
* Arguments are **positional** (no named arguments).
* Too many arguments is a runtime error.

```c
// Error: too many arguments
// let p = Point(1, 2, 3);
```

## Field Access

Struct instances are maps, so field access uses dot syntax:

```c
let p = Point(5, 7);
print(p.x, p.y);
```

Fields are mutable (you can reassign):

```c
p.x = 10;
```

## Notes

* Structs do not support methods or inheritance.
* Default expressions are evaluated when the struct is declared.
* Struct instances expose an internal `__struct` link to their defining struct map.
