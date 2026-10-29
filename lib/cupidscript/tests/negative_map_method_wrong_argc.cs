// EXPECT_FAIL
// Wrong argument count in map method call should error.

let o = {"f": fn(a, b) { return a + b; }};
o.f(1);

