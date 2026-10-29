// deepcopy() should preserve cycles without hanging

let a = list();
push(a, 1);
push(a, a);

let b = deepcopy(a);
assert(b != a, "deepcopy list returns new");
assert(len(b) == 2, "deepcopy list len");
assert(b[0] == 1, "deepcopy list element");
assert(b[1] == b, "deepcopy list self-cycle");

let m = map();
m["self"] = m;

let n = deepcopy(m);
assert(n != m, "deepcopy map returns new");
assert(n["self"] == n, "deepcopy map self-cycle");

print("stdlib_deepcopy_cycles ok");
