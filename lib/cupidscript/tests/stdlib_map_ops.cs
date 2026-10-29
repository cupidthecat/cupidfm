// Stdlib map primitives: mget/mset/mhas/mdel/keys/values/items

let m = map();

assert(!mhas(m, "a"), "empty has false");
assert(is_nil(mget(m, "a")), "missing get nil");

mset(m, "a", 1);
assert(mhas(m, "a"), "has true");
assert(mget(m, "a") == 1, "get value");

mset(m, "b", 2);

let ks = keys(m);
assert(len(ks) == 2, "keys len");
assert(contains(ks, "a"), "keys contains a");
assert(contains(ks, "b"), "keys contains b");

let vs = values(m);
assert(len(vs) == 2, "values len");
assert(contains(vs, 1), "values contains 1");
assert(contains(vs, 2), "values contains 2");

let its = items(m);
assert(len(its) == 2, "items len");
// each item is [key, value]
let seen = map();
for kv in its {
  assert(len(kv) == 2, "pair len");
  mset(seen, kv[0], kv[1]);
}
assert(mget(seen, "a") == 1, "items a");
assert(mget(seen, "b") == 2, "items b");

assert(mdel(m, "a"), "mdel true");
assert(!mhas(m, "a"), "deleted");

print("stdlib_map_ops ok");
