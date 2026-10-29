// contains() uses deep equality for lists/maps

let ll = [[1, 2], [3]];
assert(contains(ll, [1, 2]), "contains deep list");
assert(!contains(ll, [1, 9]), "contains deep list false");

let mm = [{"a": 1}, {"b": 2}];
assert(contains(mm, {"a": 1}), "contains deep map");
assert(!contains(mm, {"a": 2}), "contains deep map false");

// Default/pointer-equality branch (functions/strbuf/etc.)
fn f() { return 1; }
let fs = [f];
assert(contains(fs, f), "contains function identity");

let b = strbuf();
let bs = [b];
assert(contains(bs, b), "contains strbuf identity");

print("stdlib_contains_deep ok");
