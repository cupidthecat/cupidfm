// Force list literal growth past initial cap (8) to hit realloc branch in parser

let xs = [0,1,2,3,4,5,6,7,8];
assert(is_list(xs), "list literal type");
assert(len(xs) == 9, "list literal len 9");
assert(xs[0] == 0, "list[0]");
assert(xs[8] == 8, "list[8]");

print("list_literal_realloc ok");
