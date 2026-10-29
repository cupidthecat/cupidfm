# Tuples

Tuples hold an ordered, fixed set of fields. Fields may be positional or named.

```cs
let empty = ();
let single = (42,);
let point = (10, 20);
let named = (x: 10, y: 20);
assert(point[0] == 10, "positional access");
assert(named.x == 10 && named[1] == 20, "named and positional access");
assert(typeof(point) == "tuple" && len(point) == 2, "tuple type and length");
let [x, y] = point;
assert(x == 10 && y == 20, "destructuring");
let totals = [a + b for [a, b] in [(1, 2), (3, 4)]];
assert(totals[1] == 7, "tuple fields in comprehensions");
```

The comma distinguishes a single-element tuple from a grouped expression. Named fields can also be read by their zero-based position. Assigning a tuple to another variable retains that tuple; it does not copy nested containers.

Script code cannot replace tuple fields. Containers inside a tuple can still be changed:

```cs
let value = ([1, 2], "label");
push(value[0], 3);
assert(value[0][2] == 3, "the nested list is mutable");
```

List, map, and set comprehensions can destructure tuple elements. A missing field in a comprehension's two-name pattern binds nil.

The [embedding API](Embedding-API) provides tuple builders and retained getters for host applications.
