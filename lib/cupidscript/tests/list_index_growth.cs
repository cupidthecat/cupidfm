// List index assignment grows list

let xs = [1];
assert(len(xs) == 1, "len 1");

xs[3] = 99;
assert(len(xs) == 4, "len grows to 4");
assert(xs[0] == 1, "keeps old");
assert(is_nil(xs[1]), "fills nil 1");
assert(is_nil(xs[2]), "fills nil 2");
assert(xs[3] == 99, "set value");

// out-of-bounds read yields nil
assert(is_nil(xs[999]), "oob read -> nil");

print("list_index_growth ok");
