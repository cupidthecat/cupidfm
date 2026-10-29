// Tuple lengths must agree with indexing and destructuring.
assert(len(()) == 0, "empty tuple length");
assert(len((7,)) == 1, "single tuple length");
let point = (10, 20);
assert(typeof(point) == "tuple", "tuple type");
assert(len(point) == 2, "positional tuple length");
let named = (name: "Cupid", count: 3);
assert(len(named) == 2, "named tuple length");
let nested = ((1, 2), [3, 4]);
assert(len(nested) == 2 && len(nested[0]) == 2, "nested tuple length");
