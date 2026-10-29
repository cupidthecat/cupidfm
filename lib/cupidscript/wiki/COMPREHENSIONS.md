# Comprehensions

Comprehensions provide a concise, readable syntax for creating lists, maps, and sets by transforming and filtering existing collections. They bring functional programming elegance to CupidScript with a familiar Python-like syntax.

> ⚠️ **IMPORTANT: List Iteration Variable Order**
> 
> When iterating over lists with two variables `for var1, var2 in list`, CupidScript assigns:
> - **`var1` = VALUE** (the element)
> - **`var2` = INDEX** (the position)
> 
> This is opposite to Python's `enumerate()` and may be counter-intuitive. For the standard **(index, value)** order, use the `enumerate()` function:
> ```cupidscript
> // Counter-intuitive native order (value, index)
> let indexed = {idx: fruit for fruit, idx in ["a", "b"]}
> // {0: "a", 1: "b"}
> 
> // Standard order (index, value) - RECOMMENDED
> let indexed = {idx: fruit for [idx, fruit] in enumerate(["a", "b"])}
> // {0: "a", 1: "b"}
> ```
> See [List Iteration Order](#list-iteration-order) for details.

## Table of Contents

- [Overview](#overview)
- [List Comprehensions](#list-comprehensions)
  - [Basic Syntax](#basic-syntax)
  - [With Filters](#with-filters)
  - [From Different Iterables](#from-different-iterables)
  - [List Iteration Order](#list-iteration-order)
- [Map Comprehensions](#map-comprehensions)
  - [Basic Map Comprehensions](#basic-map-comprehensions)
  - [Transforming Maps](#transforming-maps)
  - [Creating Maps from Lists](#creating-maps-from-lists)
- [Set Comprehensions](#set-comprehensions)
  - [Basic Set Comprehensions](#basic-set-comprehensions)
  - [Filtering with Sets](#filtering-with-sets)
  - [From Different Iterables](#from-different-iterables-1)
- [Nested Comprehensions](#nested-comprehensions)
- [List Destructuring in Comprehensions](#list-destructuring-in-comprehensions)
  - [Basic Syntax](#basic-syntax-1)
  - [Common Use Cases](#common-use-cases)
  - [Important Limitations](#important-limitations)
- [Multiple Iterators](#multiple-iterators)
  - [Basic Syntax](#basic-syntax-1)
  - [Flattening Nested Lists](#flattening-nested-lists)
  - [Cartesian Products](#cartesian-products)
  - [With Filters](#with-filters-1)
  - [Dependent Iterables](#dependent-iterables)
  - [Map Comprehensions with Multiple Iterators](#map-comprehensions-with-multiple-iterators)
  - [Iteration Order](#iteration-order)
  - [Supported Iterables](#supported-iterables)
  - [Performance Considerations](#performance-considerations)
  - [Common Patterns](#common-patterns)
- [Advanced Patterns](#advanced-patterns)
  - [Complex Filters](#complex-filters)
  - [Function Calls](#function-calls)
  - [Complex Expressions](#complex-expressions)
  - [Conditional Expression Syntax](#conditional-expression-syntax)
- [Comparison with Loops](#comparison-with-loops)
- [Best Practices](#best-practices)
- [Performance Notes](#performance-notes)
- [Examples](#examples)
- [See Also](#see-also)

## Overview

Comprehensions transform the common pattern of:
1. Create empty collection
2. Loop over source
3. Transform/filter elements
4. Add to collection

Into a single, expressive line of code.

**Benefits:**
- More readable than loops for simple transformations
- Familiar to Python/JavaScript developers
- Functional programming style
- Less boilerplate code
- Clear intent

## List Comprehensions

### Basic Syntax

```cupidscript
// General form:
// [expression for variable in iterable]

let squares = [x * x for x in range(10)]
// [0, 1, 4, 9, 16, 25, 36, 49, 64, 81]

let doubled = [n * 2 for n in [1, 2, 3, 4, 5]]
// [2, 4, 6, 8, 10]

// With conditional expressions (Python-style if-else)
let signs = ["positive" if n > 0 else "negative" for n in [-1, 0, 1, 2]]
// ["negative", "negative", "positive", "positive"]

// Traditional ternary operator also works
let abs_vals = [n >= 0 ? n : -n for n in [-5, 3, -2]]
// [5, 3, 2]
```

### With Filters

Add an `if` clause to filter elements:

```cupidscript
// [expression for variable in iterable if condition]

let evens = [x for x in range(20) if x % 2 == 0]
// [0, 2, 4, 6, 8, 10, 12, 14, 16, 18]

let long_words = [w for w in words if len(w) > 5]

// Complex filter with AND logic
let special = [x for x in range(100) if (x % 3 == 0 && x % 5 != 0)]
```

### From Different Iterables

Comprehensions work with lists, maps, and ranges:

```cupidscript
// From range
let numbers = [i for i in range(1, 6)]
// [1, 2, 3, 4, 5]

// From list - single variable gets element
let items = ["a", "b", "c"]
let upper = [upper(item) for item in items]
// ["A", "B", "C"]

// From map - single variable gets keys
let map_data = {a: 1, b: 2, c: 3}
let keys = [k for k in map_data]
// ["a", "b", "c"]

// From map - two variables (ONLY in comprehensions!)
let pairs = [k + "=" + to_str(v) for k, v in map_data]
// ["a=1", "b=2", "c=3"]
```

**Note:** The two-variable `for k, v in map` syntax is **special to comprehensions** and does not work in regular `for` loops. In regular loops, use `items(map)` instead.

### List Iteration Order

**⚠️ CRITICAL: Understand the Variable Order**

When using two variables with list iteration in **regular for loops**, CupidScript uses **(value, index)** order:

```cupidscript
let fruits = ["apple", "banana", "cherry"]

// Regular for loop: VALUE FIRST, INDEX SECOND
for fruit, idx in fruits {
    print(idx, fruit)  // 0 apple, 1 banana, 2 cherry
}
```

**In comprehensions**, you can use the same syntax, but it may be confusing:

```cupidscript
// Comprehension with native syntax (value, index)
let indexed = {idx: fruit for fruit, idx in fruits}
// {0: "apple", 1: "banana", 2: "cherry"}
```

**This is opposite to most languages!** Python, JavaScript, and others typically use **(index, value)** order.

#### Recommended Solution: Use `enumerate()` with Destructuring

For the familiar **(index, value)** order in comprehensions, use the `enumerate()` built-in function with **destructuring syntax**:

```cupidscript
let fruits = ["apple", "banana", "cherry"]

// enumerate() returns [[index, value], ...] pairs
print(enumerate(fruits))
// [[0, "apple"], [1, "banana"], [2, "cherry"]]

// ✅ Use destructuring in comprehensions for clean (index, value) syntax
let indexed = {idx: fruit for [idx, fruit] in enumerate(fruits)}
// {0: "apple", 1: "banana", 2: "cherry"}

// Transform with both index and value
let labeled = [to_str(idx) + ": " + fruit for [idx, fruit] in enumerate(fruits)]
// ["0: apple", "1: banana", "2: cherry"]
```

**Destructuring in comprehensions** allows you to unpack list elements directly in the `for` clause:

```cupidscript
// Destructure any list of pairs
let pairs = [[1, "a"], [2, "b"], [3, "c"]]
let swapped = [[v, k] for [k, v] in pairs]
// [["a", 1], ["b", 2], ["c", 3]]

// Extract specific elements
let firsts = [a for [a, b] in pairs]  // [1, 2, 3]
let seconds = [b for [a, b] in pairs] // ["a", "b", "c"]
```

> **Important Limitation:** Destructuring syntax `for [a, b] in ...` **ONLY works in comprehensions**, not in regular `for` loops.

**In regular for loops**, you must access pair elements by index or use single-variable iteration:

```cupidscript
// ❌ Does NOT work in regular loops
// for [idx, fruit] in enumerate(fruits) { ... }

// ✅ Works - access by index
for pair in enumerate(fruits) {
    let idx = pair[0]
    let fruit = pair[1]
    print(idx, fruit)  // 0 apple, 1 banana, 2 cherry
}

// ✅ Works - native two-variable syntax (but value comes first!)
for fruit, idx in fruits {
    print(idx, fruit)
}
```

#### When to Use Each Pattern

| Pattern | Use When | Works In | Example |
|---------|----------|----------|----------|
| `for item in list` | Only need the value | Loops & comprehensions | `[upper(w) for w in words]` |
| `for val, idx in list` | Native two-variable iteration | Loops & comprehensions | `for fruit, i in fruits { print(i, fruit) }` |
| `for [idx, val] in enumerate(list)` | **RECOMMENDED** - Standard (index, value) order | **Comprehensions ONLY** | `[to_str(i) + ": " + v for [i, v] in enumerate(items)]` |
| `for pair in enumerate(list)` | Need index and value in loops | Loops only | `let i = pair[0], v = pair[1]` |

**Best Practice:** 
- In **comprehensions**: Always use `enumerate()` with destructuring `for [idx, val] in enumerate(list)` when you need both index and value for maximum clarity and compatibility with other languages.
- In **regular loops**: Use `for pair in enumerate(list)` and access `pair[0]` for index and `pair[1]` for value, OR use native `for val, idx in list` if you remember value comes first.

## Map Comprehensions

### Basic Map Comprehensions

```cupidscript
// General form:
// {key_expr: value_expr for var in iterable}
// {key_expr: value_expr for key_var, val_var in map}  // For maps only

let numbers = {a: 1, b: 2, c: 3}

// Two-variable iteration over maps (ONLY in comprehensions!)
let doubled = {k: v * 2 for k, v in numbers}
// {a: 2, b: 4, c: 6}

// With filter
let high_values = {k: v for k, v in numbers if v > 1}
// {b: 2, c: 3}
```

**IMPORTANT:** The `for k, v in map` syntax **ONLY works in comprehensions**, NOT in regular `for` loops. Use `items(map)` for key-value iteration in regular loops.

### Transforming Maps

Map comprehensions support **arbitrary expressions as keys**, enabling powerful transformations:

```cupidscript
let data = {a: 1, b: 2, c: 3}

// ✅ Swap keys and values
let swapped = {v: k for k, v in data}
// {1: "a", 2: "b", 3: "c"}

// ✅ Transform keys with functions
let upper_keys = {upper(k): v for k, v in data}
// {A: 1, B: 2, C: 3}

// ✅ String concatenation in keys
let transformed = {k + "_new": v * 10 for k, v in data}
// {a_new: 10, b_new: 20, c_new: 30}

// ✅ Complex key expressions
let complex = {upper(k) + "_" + to_str(v): v * v for k, v in data}
// {A_1: 1, B_2: 4, C_3: 9}

// ✅ Numeric key transformations
let by_value = {v * 100: k for k, v in data}
// {100: "a", 200: "b", 300: "c"}

// ✅ Transform values only
let doubled = {k: v * 2 for k, v in data}
// {a: 2, b: 4, c: 6}
```

### Creating Maps from Lists

```cupidscript
// Single variable iteration over range
let squares_map = {i: i * i for i in range(5)}
// {0: 0, 1: 1, 2: 4, 3: 9, 4: 16}

// Two variable iteration over map
let fruits_map = {0: "apple", 1: "banana", 2: "cherry"}
let swapped = {v: k for k, v in fruits_map}
// {apple: 0, banana: 1, cherry: 2}

// ✅ Complex key expressions now supported!
let doubled_map = {k * 10: v * 2 for k, v in fruits_map if k > 0}
// {10: "bananabanana", 20: "cherrycherry"}

// ✅ Transform both keys and values with filter
let transformed = {k + 100: upper(v) for k, v in fruits_map if k > 0}
// {101: "BANANA", 102: "CHERRY"}
```

## Set Comprehensions

Set comprehensions create unique collections by automatically removing duplicates. They use the same syntax as list comprehensions but with `#{...}` instead of `[...]`.

### Basic Set Comprehensions

```cupidscript
// General form:
// #{expression for variable in iterable}

let squares = #{x * x for x in range(10)}
// #{0, 1, 4, 9, 16, 25, 36, 49, 64, 81}

let evens = #{x for x in range(20) if x % 2 == 0}
// #{0, 2, 4, 6, 8, 10, 12, 14, 16, 18}

// Duplicates are automatically removed
let digits = #{x % 10 for x in range(25)}
// #{0, 1, 2, 3, 4, 5, 6, 7, 8, 9}
```

### Filtering with Sets

```cupidscript
// With filter condition
let positive_evens = #{x for x in range(-10, 11) if x > 0 && x % 2 == 0}
// #{2, 4, 6, 8, 10}

// Extract unique values
let data = [1, 2, 2, 3, 3, 3, 4, 4, 4, 4]
let unique = #{x for x in data}
// #{1, 2, 3, 4}

// Filter and transform
let lengths = #{len(word) for word in ["hi", "hello", "hey", "world"]}
// #{2, 3, 5}
```

### From Different Iterables

```cupidscript
// From lists
let words = ["apple", "banana", "cherry", "apple"]
let unique_upper = #{upper(w) for w in words}
// #{APPLE, BANANA, CHERRY}

// From maps (iterates over keys)
let data = {a: 1, b: 2, c: 3}
let key_set = #{k for k in data}
// #{a, b, c}

// From map key-value pairs
let value_set = #{v for k, v in data}
// #{1, 2, 3}

// From ranges
let multiples_of_3 = #{x for x in range(1, 31) if x % 3 == 0}
// #{3, 6, 9, 12, 15, 18, 21, 24, 27, 30}

// From sets (transform unique values)
let s = #{1, 2, 3, 4, 5}
let doubled = #{x * 2 for x in s}
// #{2, 4, 6, 8, 10}
```

## Nested Comprehensions

Comprehensions can be nested to create multi-dimensional structures:

```cupidscript
// Multiplication table
let table = [[i * j for j in range(1, 6)] for i in range(1, 6)]
// [[1,2,3,4,5], [2,4,6,8,10], ...]

// Nested comprehensions for coordinate generation
let rows = [[x, y] for x in range(3)]
let coords = [row for row in [[x, y] for x in range(3) for y in [0, 1, 2]]]
// Note: For complex nested iteration, use explicit loops

// Nested maps
let grid = {i: {j: i * 10 + j for j in range(3)} for i in range(3)}
// {0: {0:0, 1:1, 2:2}, 1: {0:10, 1:11, 2:12}, ...}
```

## List Destructuring in Comprehensions

Comprehensions support **destructuring syntax** for unpacking list/tuple elements directly in the `for` clause:

### Basic Syntax

```cupidscript
// General form:
// [expr for [var1, var2] in iterable]
// {key: val for [var1, var2] in iterable}

// Destructure pairs
let pairs = [[1, "a"], [2, "b"], [3, "c"]]
let swapped = [[v, k] for [k, v] in pairs]
// [["a", 1], ["b", 2], ["c", 3]]
```

### Common Use Cases

**With `enumerate()`** - the most common pattern:

```cupidscript
let fruits = ["apple", "banana", "cherry"]

// Clean (index, value) order
let indexed = {idx: fruit for [idx, fruit] in enumerate(fruits)}
// {0: "apple", 1: "banana", 2: "cherry"}

// Labeled strings
let labeled = [to_str(i) + ": " + fruit for [i, fruit] in enumerate(fruits)]
// ["0: apple", "1: banana", "2: cherry"]

// Filter by index
let evens = [fruit for [i, fruit] in enumerate(fruits) if i % 2 == 0]
// ["apple", "cherry"]
```

**Extracting from pairs**:

```cupidscript
let coords = [[0, 0], [1, 2], [3, 4]]

// Extract specific elements
let x_coords = [x for [x, y] in coords]  // [0, 1, 3]
let y_coords = [y for [x, y] in coords]  // [0, 2, 4]

// Transform using both
let distances = [x * x + y * y for [x, y] in coords]
// [0, 5, 25]
```

**Creating maps from pairs**:

```cupidscript
let pairs_list = [["name", "Alice"], ["age", 30], ["city", "NYC"]]

// Convert to map
let as_map = {k: v for [k, v] in pairs_list}
// {name: "Alice", age: 30, city: "NYC"}

// Reverse mapping
let reversed = {v: k for [k, v] in pairs_list}
```

### Important Limitations

1. **Only in comprehensions** - does NOT work in regular `for` loops:
   ```cupidscript
   // ✅ Works in comprehensions
   let result = [a + b for [a, b] in pairs]

   // ❌ Does NOT work in regular loops
   for [a, b] in pairs { ... }  // Parse error!

   // ✅ Use index access OR native two-variable syntax in regular loops
   for pair in pairs {
       let a = pair[0]
       let b = pair[1]
   }
   
   // OR (if iterating a list with two variables - remember VALUE, INDEX order!)
   for val, idx in some_list {
       // val is the element, idx is the index
   }
   ```

2. **Only simple patterns** - only supports `[var1, var2]`, not nested patterns:
   ```cupidscript
   // ✅ Supported
   for [a, b] in list

   // ❌ Not supported (nested destructuring)
   for [a, [b, c]] in list
   ```

3. **Only for lists/tuples** - the iterated value must be a list or tuple:
   ```cupidscript
   let pairs = [[1, 2], [3, 4]]

   // ✅ Works - each element is a list
   for [a, b] in pairs

   // ❌ Error - range values are integers, not lists
   for [a, b] in range(10)
   ```

### Examples

See [comprehensions_destructuring.cs](../examples/comprehensions_destructuring.cs) for comprehensive examples.

## Multiple Iterators

Comprehensions support **multiple `for` clauses** in a single comprehension, enabling powerful patterns like flattening, Cartesian products, and nested iteration.

### Basic Syntax

```cupidscript
// General form for list comprehensions:
// [expression for var1 in iterable1 for var2 in iterable2 ...]

// General form for map comprehensions:
// {key: value for var1 in iterable1 for var2 in iterable2 ...}
```

### Flattening Nested Lists

The most common use case is flattening 2D or 3D structures:

```cupidscript
// Flatten a 2D matrix
let matrix = [[1, 2], [3, 4], [5, 6]]
let flat = [item for row in matrix for item in row]
// [1, 2, 3, 4, 5, 6]

// Flatten jagged arrays
let jagged = [[1, 2, 3], [4, 5], [6]]
let flat_jagged = [x for row in jagged for x in row]
// [1, 2, 3, 4, 5, 6]

// Flatten 3D structure
let cube = [[[1, 2], [3, 4]], [[5, 6], [7, 8]]]
let flat_3d = [item for level1 in cube for level2 in level1 for item in level2]
// [1, 2, 3, 4, 5, 6, 7, 8]
```

### Cartesian Products

Generate all combinations of elements from multiple sets:

```cupidscript
// All coordinate pairs
let coords = [(x, y) for x in range(3) for y in range(3)]
// [(0,0), (0,1), (0,2), (1,0), (1,1), (1,2), (2,0), (2,1), (2,2)]

// Product combinations
let sizes = ["S", "M", "L"]
let colors = ["red", "blue"]
let products = [(size, color) for size in sizes for color in colors]
// [("S","red"), ("S","blue"), ("M","red"), ("M","blue"), ("L","red"), ("L","blue")]

// Three-way combinations
let abc = [(a, b, c) for a in [1, 2] for b in [3, 4] for c in [5, 6]]
// [(1,3,5), (1,3,6), (1,4,5), (1,4,6), (2,3,5), (2,3,6), (2,4,5), (2,4,6)]
```

### With Filters

Filters can reference variables from all iterator levels:

```cupidscript
// Pairs where x < y
let pairs = [(x, y) for x in range(5) for y in range(5) if x < y]
// [(0,1), (0,2), (0,3), (0,4), (1,2), (1,3), (1,4), (2,3), (2,4), (3,4)]

// Pythagorean-like condition
let filtered = [(x, y, z)
    for x in range(10)
    for y in range(10)
    for z in range(10)
    if x * x + y * y == z * z]

// Filter using multiple variables
let products = [x * y
    for x in range(1, 6)
    for y in range(1, 6)
    if x + y == 7]
// Products where sum equals 7: [10, 12, 12, 10]
```

### Dependent Iterables

Later iterables can depend on variables from earlier ones:

```cupidscript
// Second iterable depends on first variable
let triangular = [j for i in range(1, 5) for j in range(i)]
// [0, 0, 1, 0, 1, 2, 0, 1, 2, 3]

// Variable-length sublists
let data = [[1, 2, 3], [4, 5], [6]]
let all = [item for sublist in data for item in sublist]
// [1, 2, 3, 4, 5, 6]

// Range based on previous variable
let var_ranges = [j for i in [2, 3, 1] for j in range(i)]
// [0, 1, 0, 1, 2, 0]
```

### Map Comprehensions with Multiple Iterators

Map comprehensions also support multiple iterators:

```cupidscript
// Lookup table
let lookup = {x * 10 + y: x + y for x in range(3) for y in range(3)}
// {0:0, 1:1, 2:2, 10:1, 11:2, 12:3, 20:2, 21:3, 22:4}

// With filter
let diagonal = {x: x for x in range(5) for y in range(5) if x == y}
// {0:0, 1:1, 2:2, 3:3, 4:4}

// Flatten into map
let nested_lists = [[1, 2], [3, 4]]
let item_map = {item: item * 2 for lst in nested_lists for item in lst}
// {1:2, 2:4, 3:6, 4:8}
```

### Iteration Order

Iteration proceeds left-to-right, with rightmost iterator varying fastest (like nested loops):

```cupidscript
// Equivalent to:
// for x in range(2) {
//     for y in range(3) {
//         result.push((x, y))
//     }
// }

let pairs = [(x, y) for x in range(2) for y in range(3)]
// [(0,0), (0,1), (0,2), (1,0), (1,1), (1,2)]
//  ^^ first x value with all y values, then second x value with all y values
```

### Supported Iterables

All iterable types work with multiple iterators:

```cupidscript
// List and range
let mixed1 = [c for item in [10, 20] for c in range(item, item + 2)]
// [10, 11, 20, 21]

// String and list
let chars = [c for word in ["hi", "go"] for c in word]
// ["h", "i", "g", "o"]

// Map and list (in list comprehensions only - maps don't support string iteration)
let from_maps = [k for m in [{a: 1}, {b: 2}] for k in m]
// ["a", "b"]
```

### Performance Considerations

Multiple iterators create nested iteration - be mindful of complexity:

```cupidscript
// O(n²) - 100 iterations
let small = [x + y for x in range(10) for y in range(10)]

// O(n³) - 1,000 iterations
let medium = [(x, y, z) for x in range(10) for y in range(10) for z in range(10)]

// O(n⁴) - 10,000 iterations
let large = [(a, b, c, d)
    for a in range(10)
    for b in range(10)
    for c in range(10)
    for d in range(10)]
```

Use filters to reduce unnecessary iterations:

```cupidscript
// Without filter: 25 iterations, 25 results
let all_pairs = [(x, y) for x in range(5) for y in range(5)]

// With filter: 25 iterations, 10 results
let filtered_pairs = [(x, y) for x in range(5) for y in range(5) if x < y]

// Better: reduce iterations by starting from x+1
let efficient_pairs = [(x, y) for x in range(5) for y in range(x + 1, 5)]
// Only 10 iterations, 10 results
```

### Common Patterns

**Flatten and transform:**
```cupidscript
let matrix = [[1, 2], [3, 4]]
let doubled_flat = [x * 2 for row in matrix for x in row]
// [2, 4, 6, 8]
```

**All combinations meeting condition:**
```cupidscript
let valid_pairs = [(x, y)
    for x in range(10)
    for y in range(10)
    if is_valid(x, y)]
```

**Nested data extraction:**
```cupidscript
let data = [
    {name: "Alice", scores: [85, 90]},
    {name: "Bob", scores: [75, 80]}
]
let all_scores = [score for person in data for score in person["scores"]]
// [85, 90, 75, 80]
```

### Examples

See [comprehensions_multi_iterator.cs](../examples/comprehensions_multi_iterator.cs) for comprehensive examples including:
- Flattening techniques
- Cartesian products
- Mathematical operations
- String processing
- Data transformation patterns
- Map comprehensions with multiple iterators

## Advanced Patterns

### Complex Filters

Combine multiple conditions with logical operators:

```cupidscript
let filtered = [
    x for x in range(100)
    if (x % 2 == 0 && x % 5 != 0 && x > 10)
]
```

### Function Calls

Use functions in expressions or filters:

```cupidscript
fn square(x) { return x * x }
fn is_prime(x) { /* ... */ }

let squared = [square(x) for x in range(10)]

let primes = [x for x in range(2, 100) if is_prime(x)]

// Function calls in expressions
let lengths = [len(s) for s in strings]
let processed = [transform(s) for s in strings]
```

### Complex Expressions

Comprehensions support any valid expression:

```cupidscript
// Conditional expressions - two styles supported:

// 1. Python-style if-else (NEW in v1.x)
let signs = ["positive" if x > 0 else "negative" for x in numbers]
let categories = ["big" if n > 100 else "small" for n in values]

// 2. Traditional ternary operator (? :)
let abs_values = [x >= 0 ? x : -x for x in numbers]

// Both styles work identically - choose based on readability
let grades = ["PASS" if score >= 70 else "FAIL" for score in scores]
let grades2 = [score >= 70 ? "PASS" : "FAIL" for score in scores]

// String interpolation
let greetings = ["Hello, ${name}!" for name in names]

// Arithmetic
let converted = [(f - 32) * 5 / 9 for f in fahrenheit]

// Tuple creation
let points = [(x: i, y: i * i) for i in range(5)]

// Function calls
let lengths = [len(s) for s in strings]
let uppercase = [upper(s) for s in strings]
```

#### Conditional Expression Syntax

CupidScript supports **two equivalent syntaxes** for conditional expressions in comprehensions:

1. **Python-style** (recommended for readability): `value_if_true if condition else value_if_false`
2. **Ternary operator**: `condition ? value_if_true : value_if_false`

```cupidscript
// Python-style: reads naturally left-to-right
let status = ["active" if user.enabled else "inactive" for user in users]

// Ternary: condition first (traditional)
let status2 = [user.enabled ? "active" : "inactive" for user in users]

// With complex expressions
let labels = [
    "very_high" if x > 100 else "high" if x > 50 else "normal"
    for x in values
]

// Works in all comprehension types
let list_ex = ["even" if n % 2 == 0 else "odd" for n in range(10)]
let set_ex = {"pos" if n > 0 else "neg" for n in numbers}
let map_ex = {n: "big" if n > 5 else "small" for n in range(10)}
```

> **Note:** When using complex expressions with operators (like `+`), you may need to be mindful of precedence. The `if-else` has lower precedence than most operators.

## Comparison with Loops

### Syntax Differences

**Key differences between comprehensions and regular loops:**
```cupidscript
// ✅ WORKS in comprehensions
let doubled = {k: v * 2 for k, v in data}

// ❌ DOES NOT WORK in regular loops
// for k, v in data { ... }  // Parse error!

// ✅ Use items() in regular loops
for pair in items(data) {
    let k = pair[0]
    let v = pair[1]
    // ...
}
```

### Code Comparison

**Traditional loop:**
```cupidscript
let squares = []
for x in range(10) {
    push(squares, x * x)
}
```

**Comprehension:**
```cupidscript
let squares = [x * x for x in range(10)]
```

**When to use comprehensions:**
- ✅ Simple transformations
- ✅ Simple filtering
- ✅ Creating new collections
- ✅ Functional programming style
- ✅ Working with maps (two-variable iteration)

**When to use loops:**
- ❌ Complex logic
- ❌ Multiple statements per iteration
- ❌ Early exit conditions (break/continue)
- ❌ Side effects (printing, modifying external state)
- ❌ When you need `for [a, b] in ...` destructuring (use comprehensions instead)

## Best Practices

### 1. Keep It Simple

```cupidscript
// Good - clear and concise
let evens = [x for x in numbers if x % 2 == 0]

// Bad - too complex, use a loop
let result = [
    process(transform(x)) if validate(x) and check(x) else default(x)
    for x in data
    if complex_condition(x)
]
```

### 2. Use Meaningful Variable Names

```cupidscript
// Good
let user_ids = [user["id"] for user in users]

// Less clear
let ids = [u["id"] for u in users]
```

### 3. Limit Nesting

```cupidscript
// Good - clear nested comprehension
let matrix = [[i * j for j in range(5)] for i in range(5)]

// Avoid - too deeply nested
let result = [[[f(x, y, z) for z in a] for y in b] for x in c]
```

### 4. Consider Readability

```cupidscript
// If comprehension is hard to read, use a loop:
let result = []
for item in items {
    if (complex_condition(item)) {
        let processed = transform(item)
        push(result, processed)
    }
}
```

### 5. Don't Abuse for Side Effects

```cupidscript
// Bad - side effects in comprehension
let _ = [print(x) for x in items]  // Don't do this!

// Good - use a loop for side effects
for item in items {
    print(item)
}
```

## Performance Notes

### Eager Evaluation

Comprehensions are **eagerly evaluated** - they immediately create the full collection:

```cupidscript
let big = [x for x in range(1000000)]  // Creates all million elements now
```

For lazy evaluation, use explicit loops or generators (if available).

### Memory Efficiency

```cupidscript
// Memory efficient - processes one at a time
for x in range(1000000) {
    if (x % 1000 == 0) {
        print(x)
    }
}

// Less efficient - creates full filtered list
let filtered = [x for x in range(1000000) if x % 1000 == 0]
for x in filtered {
    print(x)
}
```

### Optimization Tips

1. **Filter early** - put filters in comprehension rather than filtering after
   ```cupidscript
   // Better
   let result = [process(x) for x in data if is_valid(x)]
   
   // Worse  
   let temp = [process(x) for x in data]
   let result = [x for x in temp if is_valid(x)]
   ```

2. **Avoid redundant comprehensions**
   ```cupidscript
   // Inefficient - two passes
   let temp = [x * 2 for x in data]
   let result = [x + 1 for x in temp]
   
   // Better - one pass
   let result = [x * 2 + 1 for x in data]
   ```

3. **Use appropriate data structures**
   ```cupidscript
   // For lookups, create map
   let index = {item: true for i, item in list}
   
   // Then check membership quickly
   if (index[x] == true) { /* ... */ }
   ```

## Examples

See the [examples directory](../examples/):
- [comprehensions.cs](../examples/comprehensions.cs) - Basic comprehension patterns
- [comprehensions_advanced.cs](../examples/comprehensions_advanced.cs) - Advanced techniques and real-world examples
- [comprehensions_conditional_expr.cs](../examples/comprehensions_conditional_expr.cs) - Python-style if-else conditional expressions
- [comprehensions_multi_iterator.cs](../examples/comprehensions_multi_iterator.cs) - Multiple iterator patterns and use cases
- [comprehensions_complex_keys.cs](../examples/comprehensions_complex_keys.cs) - Complex key expressions in map comprehensions
- [comprehensions_destructuring.cs](../examples/comprehensions_destructuring.cs) - List destructuring patterns with enumerate()
- [sets_demo.cs](../examples/sets_demo.cs) - Set literals, comprehensions, and operations
- [sets_advanced.cs](../examples/sets_advanced.cs) - Real-world set use cases

## See Also

- [Collections](Collections.md) - Lists, maps, and sets
- [Control Flow](Control-Flow.md) - For loops and iteration
- [Functions and Closures](Functions-and-Closures.md) - Functional programming
- [Language Syntax](Language-Syntax.md) - General syntax reference
