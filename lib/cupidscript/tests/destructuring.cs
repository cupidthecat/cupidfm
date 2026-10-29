// Destructuring in let

let [a, b, c] = [1, 2];
assert(a == 1, "list destruct a");
assert(b == 2, "list destruct b");
assert(c == nil, "list destruct missing -> nil");

let {x, y} = {"x": 10, "y": 20};
assert(x == 10, "map destruct x");
assert(y == 20, "map destruct y");

let {m, n} = {"m": 7};
assert(m == 7, "map destruct m");
assert(n == nil, "map destruct missing -> nil");

let [_, z] = [5, 6];
assert(z == 6, "underscore ignore");

let {k: v} = {"k": 42};
assert(v == 42, "map alias");

print("destructuring ok");
