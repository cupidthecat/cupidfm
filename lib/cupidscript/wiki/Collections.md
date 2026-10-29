# Collections (list / map / set)

## Table of Contents

- [Lists](#lists)
- [Maps](#maps)
- [Sets](#sets)
- [Comprehensions](#comprehensions)
- [Destructuring](#destructuring)
- [Data Quality-of-Life Functions](#data-quality-of-life-functions)

## Lists

### Create

```c
let xs = list();
let ys = [];           // empty list literal
let zs = [1, 2, 3];    // list literal with elements
```

### Spread

Use `...` to expand a list literal:

```c
let a = [1, 2, 3];
let b = [0, ...a, 4];  // [0, 1, 2, 3, 4]
let c = [...a, 5, 6];  // [1, 2, 3, 5, 6] - spread can be at first position
```

### Push / Pop

```c
push(xs, 123);
let v = pop(xs);
```

* `push(list, value)` appends
* `pop(list)` returns last element or `nil` if empty

### Length

```c
len(xs)
```

### Indexing

```c
xs[0]
xs[i]
```

Rules:

* index must be `int`
* negative or out of range returns `nil`

### Assigning by index

```c
xs[2] = 999;
```

This can grow the list:

* Missing intermediate elements become `nil`

### Insert / Remove

```c
insert(xs, index, value)  // insert at position
remove(xs, index)         // remove and return element at index
```

### Slice

```c
let sub = slice(xs, start, length);
```

Returns a new list with elements from `start` (inclusive) for `length` elements.

### Extend

```c
extend(xs, ys); // appends all elements of ys into xs
```

### Index Of

```c
let i = index_of(xs, 123); // returns index or -1

### Utilities

```c
list_unique(xs)   // remove duplicates (preserve order)
list_flatten(xs)  // flatten one level
list_chunk(xs, 2) // [[...], ...]
list_compact(xs)  // remove nils
list_sum(xs)      // sum numbers (nil ignored)
```
```

### Sort

```c
sort(xs); // in-place, default comparison (insertion)
sort(xs, "quick");
sort(xs, "merge");

fn desc(a, b) { return b - a; }
sort(xs, desc, "quick"); // custom comparator + algorithm
```

## Maps

Maps accept **any value** as a key.

Key equality follows `==`:

* `int` and `float` compare by numeric value (so `1` equals `1.0`)
* `string` compares by content
* Lists/maps/functions compare by identity (same object)

### Create

```c
let m = map();
let m2 = {};                          // empty map literal
let m3 = {"name": "Frank", "age": 30}; // map literal
let m4 = {1: "one", true: "yes", nil: "none"};
```

### Spread

Use `...` to expand a map literal (later keys override earlier keys):

```c
let defaults = {theme: "dark", size: 12};
let config = {...defaults, size: 14};     // {theme: "dark", size: 14}
let merged = {...defaults, ...config};    // spread can be at first position
```

### Set / Get

## Comprehensions

CupidScript supports **list and map comprehensions** for concise collection creation:

### List Comprehensions

Transform and filter iterables in a single expression:

```c
// Basic syntax: [expression for variable in iterable]
let squares = [x * x for x in range(10)]
// [0, 1, 4, 9, 16, 25, 36, 49, 64, 81]

// With filter: [expression for variable in iterable if condition]
let evens = [x for x in range(20) if x % 2 == 0]
// [0, 2, 4, 6, 8, 10, 12, 14, 16, 18]

// From lists
let words = ["hello", "world", "cupid"]
let uppercase = [upper(w) for w in words]
// ["HELLO", "WORLD", "CUPID"]

// From maps (note: for k, v in map syntax only works in comprehensions)
let data = {a: 1, b: 2, c: 3}
let pairs = [k + ":" + to_str(v) for k, v in data]
// ["a:1", "b:2", "c:3"]
```

### Map Comprehensions

Create or transform maps:

```c
// Basic syntax: {key_expr: value_expr for key_var, value_var in iterable}
let numbers = {a: 1, b: 2, c: 3}
let doubled = {k: v * 2 for k, v in numbers}
// {a: 2, b: 4, c: 6}

// With filter
let high_values = {k: v for k, v in numbers if v > 1}
// {b: 2, c: 3}

// From list to map
// ⚠️ IMPORTANT: When iterating lists with 'for var1, var2 in list':
//   - var1 receives the VALUE (element)
//   - var2 receives the INDEX (position)
// This is counter-intuitive! For standard (index, value) order, use enumerate():
let fruits = ["apple", "banana", "cherry"]

// Counter-intuitive order (value, index)
let indexed = {idx: fruit for fruit, idx in fruits}
// {0: "apple", 1: "banana", 2: "cherry"}

// RECOMMENDED: Use enumerate() for standard (index, value) order
let indexed2 = {idx: fruit for [idx, fruit] in enumerate(fruits)}
// {0: "apple", 1: "banana", 2: "cherry"}

// Map comprehensions support arbitrary key expressions!
let by_length = {len(fruit): fruit for fruit, idx in fruits}
// {5: "apple", 6: "cherry"}  (banana overwritten by cherry, both length 6)

let prefixed = {fruit + "_item": idx for fruit, idx in fruits}
// {apple_item: 0, banana_item: 1, cherry_item: 2}
```

### Nested Comprehensions

Create multi-dimensional structures:

```c
// Multiplication table
let table = [[i * j for j in range(1, 6)] for i in range(1, 6)]

// Coordinate pairs
let coords = [(x, y) for x in range(3) for y in range(3)]
// [(0,0), (0,1), (0,2), (1,0), ...]
```

**See [Comprehensions](COMPREHENSIONS.md) for complete documentation.**

## Destructuring

Lists and maps can be destructured in `let` declarations:

```c
let [a, b] = [1, 2];
let {x, y} = {"x": 10, "y": 20};
let {key: alias} = {"key": "value"};
```

Rest patterns capture remaining elements:

```c
let [a, b, ...rest] = [1, 2, 3, 4];
let {x, ...other} = {x: 10, y: 20, z: 30};
```

Missing entries produce `nil`. Use `_` to ignore a binding.

```c
mset(m, "name", "Frank");
print(mget(m, "name"));

// non-string keys
mset(m, 42, "answer");
print(mget(m, 42));
```

* Missing key returns `nil`

### Has Key

```c
mhas(m, "name") // bool
mhas(m, 42)      // bool
```

### Keys

```c
let ks = keys(m); // list of keys (any value)
```

### Values

```c
let vs = values(m); // list of values
```

### Items (key-value pairs)

```c
let pairs = items(m); // list of [key, value] lists
```

### Map Iteration

**In comprehensions:**
```c
let data = {a: 1, b: 2, c: 3}
let doubled = {k: v * 2 for k, v in data}  // Works in comprehensions
```

**In regular loops:**
```c
let data = {a: 1, b: 2, c: 3}

// WRONG: for k, v in data {...} does NOT work in regular loops
// RIGHT: Use items() to get key-value pairs
for pair in items(data) {
    let k = pair[0]
    let v = pair[1]
    print(k, "=", v)
}
```

**Key difference:** The `for k, v in map` syntax only works in comprehensions, not in regular `for` loops.

## Sets

Sets store unique values (based on `==`). CupidScript provides both function-based and literal syntax for working with sets.

### Create

```c
// Function-based creation
let s = set();
let s2 = set([1, 2, 2, 3]); // duplicates ignored
let s3 = set({a: 1, b: 2}); // keys from map

// Set literals (recommended)
let empty = #{};                  // empty set
let nums = #{1, 2, 3};           // set with elements
let mixed = #{"apple", 42, true}; // mixed types

// Set comprehensions
let squares = #{x * x for x in range(10)};
let evens = #{x for x in range(20) if x % 2 == 0};
let unique_lengths = #{len(word) for word in ["hello", "world", "hi"]};
```

### Spread

Use `...` to expand sets or lists into set literals:

```c
let a = #{1, 2, 3};
let b = #{0, ...a, 4};        // #{0, 1, 2, 3, 4}
let list = [1, 2, 2, 3];
let from_list = #{...list};   // #{1, 2, 3} - duplicates removed
```

### Methods

Sets support method-style operations using dot syntax:

```c
let s = #{1, 2, 3};

// Add element
s.add(4);           // returns true if added, false if already present
s.add(2);           // returns false (already exists)

// Check membership
s.contains(3);      // true
s.contains(99);     // false

// Remove element
s.remove(2);        // returns true if removed, false if not found
s.remove(99);       // returns false

// Size
let count = s.size();  // 3 (after removing 2)

// Clear all elements
s.clear();          // empties the set
```

### Set Operators

CupidScript provides mathematical set operators for combining and comparing sets:

```c
let a = #{1, 2, 3, 4};
let b = #{3, 4, 5, 6};

// Union: all elements from both sets
let union = a | b;           // #{1, 2, 3, 4, 5, 6}

// Intersection: elements in both sets
let inter = a & b;           // #{3, 4}

// Difference: elements in a but not in b
let diff = a - b;            // #{1, 2}

// Symmetric difference: elements in either set but not both
let sym_diff = a ^ b;        // #{1, 2, 5, 6}

// Operators can be chained
let result = a | b & #{4, 5}; // operators have standard precedence
```

**Operator Precedence (high to low):**
- Intersection (`&`) - binds tightest
- Symmetric difference (`^`)
- Union (`|`)
- Difference (`-`)
- Logical operators (`&&`, `||`)

### Function-Based Operations (Legacy)

For compatibility, set operations are also available as functions:

```c
set_add(s, 10);   // true if inserted
set_has(s, 10);   // true if present
set_del(s, 10);   // true if removed
```

### Values

```c
let xs = set_values(s); // list of values (order unspecified)
```

### Iteration

```c
for v in s {
  print(v);
}

// In comprehensions (recommended for transformations)
let doubled = [v * 2 for v in s];
let filtered = #{v for v in s if v > 10};
```

Additional helpers for map iteration:

* `map_values(m)` - Get all values as a list

```c
let m = {"a": 1, "b": 2, "c": 3};
for v in map_values(m) {
  print("Value:", v);
}
```

### Iteration Helpers

```c
let xs = [1, 2, 3];

// enumerate() returns [[index, value], ...] pairs
print(enumerate(xs)); // [[0,1], [1,2], [2,3]]

// Use in comprehensions with destructuring for clean (index, value) access
let labeled = [to_str(idx) + ": " + to_str(val) for [idx, val] in enumerate(xs)];
// ["0: 1", "1: 2", "2: 3"]

print(zip(["a", "b"], [10, 20, 30])); // [["a",10], ["b",20]]

fn is_even(x) { return x % 2 == 0; }
print(any(xs, is_even)); // true (2 is even)
print(all(xs, is_even)); // false (1 and 3 are odd)

print(filter(xs, is_even)); // [2]
print(map(xs, fn(x) => x * 2)); // [2, 4, 6]

fn sum(a, b) { return a + b; }
print(reduce(xs, sum)); // 6
```

## Data Quality-of-Life Functions

### Copy and Deep Copy

* `copy(x)` - Shallow copy of list or map
* `deepcopy(x)` - Deep copy with cycle detection

```c
let original = [1, 2, [3, 4]];
let shallow = copy(original);
let deep = deepcopy(original);

shallow[2][0] = 99;  // Affects original
deep[2][1] = 88;     // Does not affect original
```

### Reverse

* `reverse(list)` - In-place reversal
* `reversed(list)` - Returns new reversed list

```c
let list = [1, 2, 3, 4, 5];
reverse(list);  // list is now [5, 4, 3, 2, 1]

let rev = reversed([1, 2, 3]);  // Returns [3, 2, 1], original unchanged
```

### Contains

* `contains(container, item)` - Check if item exists

Works with lists, maps (checks keys), and strings:

```c
print(contains([1, 2, 3], 2));           // true
print(contains({a: 1, b: 2}, "a"));      // true
print(contains("hello world", "world")); // true
```

### Delete Key

```c
mdel(m, "key"); // removes the key
```

### Indexing with keys

```c
m["name"]
m[42]
```

### Field access on maps

If `m` is a map, `m.name` reads `m["name"]`.

If `m` is *not* a map, field access is a runtime error.

### Iterating Lists

```cs
// Single variable - just values
for item in [1, 2, 3] {
    print(item)
}

// Two variables - value, index
for val, idx in ["a", "b", "c"] {
    print("${idx}: ${val}")
}

// With enumerate() - index, value (matches Python order)
for [idx, val] in enumerate(["a", "b", "c"]) {
    print("${idx}: ${val}")
}
```

### Iterating Maps

```cs
let data = {name: "Alice", age: 30}

// Single variable - keys only
for key in data {
    print(key)
}

// Two variables - key, value
for key, val in data {
    print("${key} = ${val}")
}

// Destructuring with items()
for [k, v] in data.items() {
    print("${k}: ${v}")
}

// Just values
for val in data.values() {
    print(val)
}
```
