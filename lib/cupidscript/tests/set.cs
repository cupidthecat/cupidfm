// Set tests

fn list_has(xs, v) {
  for x in xs {
    if (x == v) { return true; }
  }
  return false;
}

let s = set();
assert(typeof(s) == "set", "typeof set");
assert(is_set(s), "is_set");
assert(len(s) == 0, "empty length");

assert(set_add(s, 1), "add 1");
assert(!set_add(s, 1), "duplicate add");
assert(len(s) == 1, "len after add");
assert(set_has(s, 1), "has 1");
assert(!set_has(s, 2), "missing");
assert(set_del(s, 1), "del 1");
assert(!set_del(s, 1), "del missing");
assert(len(s) == 0, "len after del");

let s2 = set([1, 2, 2, 3]);
assert(len(s2) == 3, "set from list unique");
assert(set_has(s2, 2), "set from list has");

let m = {a: 1, b: 2};
let s3 = set(m);
assert(set_has(s3, "a"), "set from map keys");
assert(set_has(s3, "b"), "set from map keys");

let s4 = set(s2);
assert(len(s4) == 3, "set from set copy");

let sum = 0;
for v in s2 { sum += v; }
assert(sum == 6, "for-in over set");

assert(contains(s2, 3), "contains set");
let vals = set_values(s2);
assert(list_has(vals, 1) && list_has(vals, 2) && list_has(vals, 3), "set_values");

print("set ok");
