let xs = [1, 2, 3,];
assert(len(xs) == 3 && xs[2] == 3, "list trailing comma");
let m = {"a": 1,};
assert(m.a == 1 && len(m) == 1, "map trailing comma");
let nested = {outer: [1, {inner: 2,},],};
assert(nested.outer[1].inner == 2, "nested trailing commas");
