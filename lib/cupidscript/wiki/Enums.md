
# Enums

## Table of Contents

- [Syntax](#syntax)
- [Rules](#rules)
- [Notes](#notes)

## Table of Contents

- [Syntax](#syntax)
- [Rules](#rules)
- [Notes](#notes)

Enums define named integer constants. Enum members are stored in a map, so values are accessed using dot syntax.

## Syntax

```c
enum Color { Red, Green = 5, Blue }

print(Color.Red);   // 0
print(Color.Green); // 5
print(Color.Blue);  // 6
```

## Rules

* Members are integers.
* If a value is omitted, it auto-increments from the previous member (starting at `0`).
* Explicit values reset the increment counter for subsequent members.

```c
enum Code { OK = 200, NotFound = 404, Gone }
// OK=200, NotFound=404, Gone=405
```

## Notes

* Enums are maps with an internal `__is_enum` flag.
* Enum values are integers and can be used in comparisons or switch cases.
