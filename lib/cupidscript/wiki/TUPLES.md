# Tuples

Tuples are **immutable, fixed-size** value groupings that provide a lightweight way to return multiple values from functions and create structured data without the overhead of maps or classes.

## Table of Contents

- [Overview](#overview)
- [Creating Tuples](#creating-tuples)
  - [Positional Tuples](#positional-tuples)
  - [Named Tuples](#named-tuples)
- [Accessing Tuple Elements](#accessing-tuple-elements)
  - [Index Access](#index-access)
  - [Field Access](#field-access)
- [Destructuring](#destructuring)
  - [Destructuring Function Returns](#destructuring-function-returns)
- [Multiple Return Values](#multiple-return-values)
- [Common Patterns](#common-patterns)
  - [Swapping Values](#swapping-values)
  - [Configuration Objects](#configuration-objects)
  - [Result/Error Pattern](#resulterror-pattern)
  - [Statistical Returns](#statistical-returns)
- [Immutability](#immutability)
- [Use Cases](#use-cases)
- [Comparison with Other Types](#comparison-with-other-types)
  - [Tuples vs. Lists](#tuples-vs-lists)
  - [Tuples vs. Maps](#tuples-vs-maps)
- [Type Checking](#type-checking)
- [Best Practices](#best-practices)
- [Performance Notes](#performance-notes)
- [Limitations](#limitations)
- [Examples](#examples)
- [See Also](#see-also)

## Overview

Tuples in CupidScript support:
- **Positional tuples**: Ordered sequence of values accessed by index
- **Named tuples**: Fields accessed by name (like lightweight structs)
- **Destructuring**: Extract values into variables
- **Immutability**: Tuple fields cannot be modified after creation
- **Type heterogeneity**: Can contain values of different types

## Creating Tuples

### Positional Tuples

Positional tuples group values in a specific order:

```cupidscript
// Basic positional tuple
let coords = (10, 20, 30)
print(coords[0])  // 10
print(coords[1])  // 20
print(coords[2])  // 30

// Single element tuple (requires trailing comma)
let single = (42,)

// Empty tuple
let empty = ()

// Mixed types
let mixed = ("hello", 42, true, nil)
```

### Named Tuples

Named tuples provide field names for clearer, self-documenting code:

```cupidscript
// Named tuple
let point = (x: 10, y: 20)
print(point.x)  // 10
print(point.y)  // 20

// Can still access positionally
print(point[0])  // 10
print(point[1])  // 20

// Multi-line for readability
let person = (
    name: "Alice",
    age: 30,
    city: "NYC"
)
```

## Accessing Tuple Elements

### Index Access

All tuples support index access (0-based):

```cupidscript
let data = (100, 200, 300)
let first = data[0]     // 100
let second = data[1]    // 200
let third = data[2]     // 300
```

### Field Access

Named tuples support field access using dot notation:

```cupidscript
let color = (r: 255, g: 128, b: 64)
let red = color.r       // 255
let green = color.g     // 128
let blue = color.b      // 64
```

## Destructuring

Tuples can be destructured to extract values into separate variables:

```cupidscript
// Basic destructuring
let coords = (10, 20, 30)
let [x, y, z] = coords
print(x)  // 10
print(y)  // 20
print(z)  // 30

// Partial destructuring
let point = (100, 200, 300)
let [a, b] = point  // Takes first two elements

// Ignore values with underscore
let data = (1, 2, 3, 4, 5)
let [first, _, third] = data  // Ignores second value

// Works with named tuples too
let person = (name: "Bob", age: 25)
let [n, a] = person
```

### Destructuring Function Returns

```cupidscript
fn get_position() {
    return (x: 100, y: 200)
}

// Use as-is
let pos = get_position()
print(pos.x, pos.y)

// Or destructure
let [x, y] = get_position()
print(x, y)
```

## Multiple Return Values

Tuples are ideal for returning multiple values from functions:

```cupidscript
fn divide_with_remainder(a, b) {
    return (
        quotient: a / b,
        remainder: a % b
    )
}

let result = divide_with_remainder(17, 5)
print(result.quotient)   // 3
print(result.remainder)  // 2

// Or destructure immediately
let [q, r] = divide_with_remainder(23, 7)
```

## Common Patterns

### Swapping Values

```cupidscript
let a = 5
let b = 10
[a, b] = (b, a)  // Swap using tuple
```

### Configuration Objects

```cupidscript
fn create_window(config) {
    // Use config.width, config.height, etc.
}

create_window((
    width: 800,
    height: 600,
    title: "My App",
    resizable: true
))
```

### Result/Error Pattern

```cupidscript
fn safe_divide(a, b) {
    if (b == 0) {
        return (ok: false, error: "Division by zero")
    }
    return (ok: true, value: a / b)
}

let result = safe_divide(10, 2)
if (result.ok) {
    print("Result:", result.value)
} else {
    print("Error:", result.error)
}
```

### Statistical Returns

```cupidscript
fn calculate_stats(numbers) {
    // ... calculations ...
    return (
        min: min_val,
        max: max_val,
        avg: average,
        sum: total
    )
}

let stats = calculate_stats([1, 2, 3, 4, 5])
print("Average:", stats.avg)
```

## Immutability

Tuples are **immutable** - their fields cannot be modified after creation:

```cupidscript
let point = (x: 10, y: 20)

// These operations are NOT supported:
// point.x = 100     // Error: cannot assign to tuple field
// point[0] = 100    // Error: cannot assign to tuple element

// Instead, create a new tuple:
let new_point = (x: 100, y: 20)
```

This immutability guarantee:
- Prevents accidental modifications
- Makes tuples safe to pass around
- Enables optimization opportunities
- Provides predictable behavior

## Use Cases

### 1. Coordinates and Points

```cupidscript
let point2d = (x: 10, y: 20)
let point3d = (x: 10, y: 20, z: 30)
let color = (r: 255, g: 128, b: 64, a: 1.0)
```

### 2. Date and Time

```cupidscript
let date = (year: 2026, month: 1, day: 27)
let time = (hour: 14, minute: 30, second: 45)
```

### 3. Range and Bounds

```cupidscript
let range = (start: 0, end: 100, step: 10)
let bounds = (min: 0, max: 255)
```

### 4. HTTP Responses

```cupidscript
fn fetch_data(url) {
    return (
        status: 200,
        body: "...",
        headers: {...}
    )
}
```

### 5. Nested Data Structures

```cupidscript
let rect = (
    topLeft: (x: 0, y: 0),
    bottomRight: (x: 100, y: 50)
)

print(rect.topLeft.x)  // 0
```

### 6. Map Keys

```cupidscript
let grid = {}
grid[(0, 0)] = "origin"
grid[(1, 0)] = "east"
grid[(0, 1)] = "north"
```

## Comparison with Other Types

### Tuples vs. Lists

| Feature | Tuple | List |
|---------|-------|------|
| Size | Fixed | Dynamic |
| Mutability | Immutable | Mutable |
| Access | Index or name | Index only |
| Performance | Faster (fixed) | Slower (dynamic) |
| Use case | Structured data | Collections |

### Tuples vs. Maps

| Feature | Tuple | Map |
|---------|-------|-----|
| Fields | Fixed at creation | Dynamic |
| Access | Compile-time names | Runtime keys |
| Type safety | Better | Less |
| Mutability | Immutable | Mutable |
| Performance | Better | Good |

## Type Checking

```cupidscript
let data = (x: 10, y: 20)
print(typeof(data))  // "tuple"
```

## Best Practices

1. **Use named tuples for clarity**: When fields have meaning, use names
   ```cupidscript
   // Good
   let point = (x: 10, y: 20)
   
   // Less clear
   let point = (10, 20)
   ```

2. **Limit tuple size**: Keep tuples small (2-5 fields recommended)
   - For larger structures, consider classes or structs

3. **Use for temporary groupings**: Tuples excel at short-lived data
   ```cupidscript
   fn get_min_max(arr) {
       return (min: calculate_min(arr), max: calculate_max(arr))
   }
   ```

4. **Destructure when appropriate**: Make code cleaner
   ```cupidscript
   let [width, height] = get_dimensions()
   ```

5. **Document field meanings**: Comment named tuple fields
   ```cupidscript
   let config = (
       timeout: 5000,      // milliseconds
       retries: 3,         // number of attempts
       backoff: 1.5        // exponential factor
   )
   ```

## Performance Notes

- Tuples are **lightweight** - minimal memory overhead
- **Field access** is O(1) for both index and name lookups
- **Immutability** allows compiler optimizations
- **No reference counting** needed for primitive fields
- **Hash-friendly** - can be used as map keys efficiently

## Limitations

1. **Fixed size**: Cannot add/remove fields after creation
2. **No methods**: Tuples don't support custom methods
3. **No rest destructuring**: Cannot use `...rest` syntax (unlike lists)
4. **Immutable**: Cannot modify fields (by design)

## Examples

See the [examples directory](../examples/):
- [tuples.cs](../examples/tuples.cs) - Basic tuple usage
- [tuple_advanced.cs](../examples/tuple_advanced.cs) - Advanced patterns

## See Also

- [Destructuring](destructuring.md)
- [Functions](functions.md)
- [Structs](structs.md)
- [Maps](maps.md)
