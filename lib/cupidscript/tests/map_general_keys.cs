let m = {};

m[1] = "one";
assert(m[1] == "one", "int key get");
assert(m[1.0] == "one", "float key matches int");

m[1.0] = "onef";
assert(m[1] == "onef", "float set updates int key");

m[true] = "yes";
assert(m[true] == "yes", "bool key");

m[nil] = "nilkey";
assert(m[nil] == "nilkey", "nil key");

let k = [1, 2];
m[k] = "listkey";
assert(m[k] == "listkey", "list key by identity");
let k2 = [1, 2];
assert(m[k2] == nil, "different list key");

assert(mhas(m, 1), "mhas int");
assert(mget(m, 1) == "onef", "mget int");

mset(m, 2, "two");
assert(mget(m, 2) == "two", "mset int");
assert(mdel(m, 2), "mdel int");
assert(mget(m, 2) == nil, "mdel removed");

let m2 = {1: "a", true: "b", nil: "c"};
assert(m2[1] == "a", "map literal int key");
assert(m2[true] == "b", "map literal bool key");
assert(m2[nil] == "c", "map literal nil key");

print("map_general_keys ok");
