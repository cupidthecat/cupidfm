// Collection utility tests

let xs = [1, 2, 2, 3, 1];
let uniq = list_unique(xs);
assert(len(uniq) == 3, "list_unique len");
assert(uniq[0] == 1 && uniq[1] == 2 && uniq[2] == 3, "list_unique order");

let flat = list_flatten([1, [2, 3], [4], 5]);
assert(len(flat) == 5, "list_flatten len");
assert(flat[1] == 2 && flat[2] == 3, "list_flatten items");

let chunks = list_chunk([1, 2, 3, 4, 5], 2);
assert(len(chunks) == 3, "list_chunk count");
assert(chunks[0][0] == 1 && chunks[0][1] == 2, "list_chunk first");
assert(len(chunks[2]) == 1 && chunks[2][0] == 5, "list_chunk last");

let compacted = list_compact([1, nil, 2, nil, 3]);
assert(len(compacted) == 3, "list_compact len");

let s1 = list_sum([1, 2, 3]);
assert(s1 == 6, "list_sum int");
let s2 = list_sum([1.5, 2, 3]);
assert(s2 == 6.5, "list_sum float");

print("collection_utils ok");
