// List helper tests

let xs = [1, 2];
let ys = [3, 4];
extend(xs, ys);
assert(len(xs) == 4, "extend length");
assert(xs[2] == 3 && xs[3] == 4, "extend contents");

assert(index_of(xs, 3) == 2, "index_of found");
assert(index_of(xs, 99) == -1, "index_of missing");

// contains for list
assert(contains(xs, 4), "contains list");

// default sort (numbers)
let nums = [3, 1, 2];
sort(nums);
assert(nums[0] == 1 && nums[1] == 2 && nums[2] == 3, "sort default numbers");

// quicksort
let nums_q = [5, 2, 4, 1, 3];
sort(nums_q, "quick");
assert(nums_q[0] == 1 && nums_q[4] == 5, "sort quick");

// mergesort
let nums_m = [5, 2, 4, 1, 3];
sort(nums_m, "merge");
assert(nums_m[0] == 1 && nums_m[4] == 5, "sort merge");

// default sort (strings)
let words = ["b", "a", "c"]; 
sort(words);
assert(words[0] == "a" && words[1] == "b" && words[2] == "c", "sort default strings");

// custom comparator (descending)
fn desc(a, b) { return b - a; }
let nums2 = [1, 3, 2];
sort(nums2, desc, "quick");
assert(nums2[0] == 3 && nums2[1] == 2 && nums2[2] == 1, "sort custom comparator");

print("list_helpers ok");
