// Lists + maps + field/index access

let xs = [10, 20];
assert(xs[0] == 10, "list index");
xs[1] = 99;
assert(xs[1] == 99, "list setindex");

push(xs, 123);
assert(len(xs) == 3, "push/len");
assert(pop(xs) == 123, "pop");
assert(len(xs) == 2, "pop len");

insert(xs, 1, 77);
assert(len(xs) == 3, "insert len");
assert(xs[1] == 77, "insert value");

assert(remove(xs, 1) == 77, "remove value");
assert(len(xs) == 2, "remove len");

let sl = slice([1, 2, 3, 4, 5], 1, 4);
assert(len(sl) == 3, "slice len");
assert(sl[0] == 2, "slice first");
assert(sl[2] == 4, "slice last");

let m = { "a": 1, "b": 2 };
assert(m["a"] == 1, "map index");

// field access on map should match m["k"]
assert(m.a == 1, "map field get");

m["c"] = 3;
assert(m.c == 3, "map field after setindex");

assert(mhas(m, "b"), "mhas");
assert(mget(m, "b") == 2, "mget");
assert(mdel(m, "b"), "mdel");
assert(!mhas(m, "b"), "mdel removed");

let ks = keys(m);
assert(contains(ks, "a"), "keys contains a");
assert(contains(ks, "c"), "keys contains c");

let vs = values(m);
assert(contains(vs, 1), "values contains 1");
assert(contains(vs, 3), "values contains 3");

let it = items(m);
assert(len(it) == len(m), "items len");

print("collections ok");
