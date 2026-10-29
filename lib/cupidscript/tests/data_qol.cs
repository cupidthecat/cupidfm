// Data QoL: copy/deepcopy/reverse/reversed/contains/map_values

let inner = [1];
let outer = [inner];

let c1 = copy(outer);
assert(c1 != outer, "copy returns new list");
assert(c1[0] == inner, "copy is shallow");

let c2 = deepcopy(outer);
assert(c2 != outer, "deepcopy returns new list");
assert(c2[0] != inner, "deepcopy copies nested list");

let xs = [1, 2, 3];
let ys = reversed(xs);
assert(xs[0] == 1, "reversed does not mutate");
assert(ys[0] == 3, "reversed content");

reverse(xs);
assert(xs[0] == 3, "reverse mutates");

assert(contains([1, 2, 3], 2), "contains list");
assert(!contains([1, 2, 3], 9), "contains list false");
assert(contains("hello", "ell"), "contains string");

let m = {"a": 1, "b": 2};
let vals = map_values(m);
assert(contains(vals, 1), "map_values contains 1");
assert(contains(vals, 2), "map_values contains 2");
assert(contains(m, "a"), "contains map key");

print("data_qol ok");
