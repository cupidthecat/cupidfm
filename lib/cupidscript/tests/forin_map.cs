// for-in over map iterates keys

let m = {"a": 1, "b": 2, "c": 3};
let keys_seen = list();

for k in m {
  push(keys_seen, k);
}

assert(len(keys_seen) == 3, "for-in map key count");
assert(contains(keys_seen, "a"), "has a");
assert(contains(keys_seen, "b"), "has b");
assert(contains(keys_seen, "c"), "has c");

print("forin_map ok");
