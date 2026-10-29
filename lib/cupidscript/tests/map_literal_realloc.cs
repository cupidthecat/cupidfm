// Force map literal growth past initial cap (8) to hit realloc branch in parser

let m = {
  "k0": 0,
  "k1": 1,
  "k2": 2,
  "k3": 3,
  "k4": 4,
  "k5": 5,
  "k6": 6,
  "k7": 7,
  "k8": 8
};

assert(is_map(m), "map literal type");
assert(len(m) == 9, "map literal len 9");
assert(m["k0"] == 0, "map literal get k0");
assert(m["k8"] == 8, "map literal get k8");

print("map_literal_realloc ok");
