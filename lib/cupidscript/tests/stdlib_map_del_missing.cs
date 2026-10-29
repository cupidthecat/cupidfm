// Exercise mdel() branches: missing key, existing key, repeated delete.

let m = map();
mset(m, "a", 1);
mset(m, "b", 2);

assert(!mdel(m, "nope"), "mdel missing -> false");
assert(mhas(m, "a"), "a still exists");

assert(mdel(m, "a"), "mdel existing -> true");
assert(!mhas(m, "a"), "a deleted");

assert(!mdel(m, "a"), "mdel deleted again -> false");
assert(mhas(m, "b"), "b still exists");

print("stdlib_map_del_missing ok");

