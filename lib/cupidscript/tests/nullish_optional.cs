// Nullish coalescing and optional chaining

let a = nil;
assert(a ?? 5 == 5, "nil coalescing");

let b = 0;
assert(b ?? 5 == 0, "non-nil coalescing");

let m = {"x": 1};
assert(m?.x == 1, "optional field access");

let n = nil;
assert(n?.x == nil, "optional field on nil");

let o = {"f": fn() { return 7; }};
assert(o?.f() == 7, "optional call");
assert(n?.f() == nil, "optional call on nil");

print("nullish_optional ok");
