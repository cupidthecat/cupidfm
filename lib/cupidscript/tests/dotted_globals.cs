// Dotted global fallback (fm.*) + map method call

// These natives exist in the sample CLI (src/main.c). This should not error.
fm.status("hello from test");

let o = {"twice": fn(x) { return x * 2; }};
assert(o.twice(21) == 42, "map method call");

print("dotted_globals ok");
