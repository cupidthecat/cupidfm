// Enhanced Standard Library Tests
// Tests for new utility functions in CupidScript

print("=== Enhanced stdlib Tests ===\n");

let passed = 0;
let failed = 0;

fn test(name, condition) {
    if (condition) {
        print("✓ " + name);
        passed = passed + 1;
    } else {
        print("✗ " + name);
        failed = failed + 1;
    }
}

// ===== String Utilities =====
print("\n--- String Utilities ---");

// str_pad_left
test("str_pad_left basic", str_pad_left("42", 5) == "   42");
test("str_pad_left with custom fill", str_pad_left("42", 5, "0") == "00042");
test("str_pad_left no padding needed", str_pad_left("hello", 3) == "hello");

// str_pad_right
test("str_pad_right basic", str_pad_right("42", 5) == "42   ");
test("str_pad_right with custom fill", str_pad_right("42", 5, "-") == "42---");
test("str_pad_right no padding needed", str_pad_right("hello", 3) == "hello");

// ===== List Utilities =====
print("\n--- List Utilities ---");

// list_unique
let l1 = [1, 2, 3, 2, 1, 4];
let unique = list_unique(l1);
test("list_unique removes duplicates", len(unique) == 4 && contains(unique, 1) && contains(unique, 4));

// list_flatten
let nested = [[1, 2], [3, 4], [5]];
let flattened = list_flatten(nested);
test("list_flatten flattens nested lists", len(flattened) == 5 && flattened[0] == 1 && flattened[4] == 5);

let mixed = [[1, 2], 3, [4, 5]];
let mixed_flat = list_flatten(mixed);
test("list_flatten handles mixed content", len(mixed_flat) == 5 && mixed_flat[2] == 3);

// list_chunk
let numbers = [1, 2, 3, 4, 5, 6, 7];
let chunked = list_chunk(numbers, 3);
test("list_chunk creates chunks", len(chunked) == 3);
test("list_chunk first chunk correct", len(chunked[0]) == 3 && chunked[0][0] == 1);
test("list_chunk last chunk correct", len(chunked[2]) == 1 && chunked[2][0] == 7);

// list_compact
let with_nils = [1, nil, 2, nil, 3];
let compacted = list_compact(with_nils);
test("list_compact removes nils", len(compacted) == 3 && compacted[0] == 1 && compacted[2] == 3);

// list_sum
let nums1 = [1, 2, 3, 4, 5];
test("list_sum integers", list_sum(nums1) == 15);

let nums2 = [1.5, 2.5, 3.0];
test("list_sum floats", list_sum(nums2) == 7.0);

// list_intersection
let a = [1, 2, 3, 4];
let b = [3, 4, 5, 6];
let inter = list_intersection(a, b);
test("list_intersection finds common elements", len(inter) == 2 && contains(inter, 3) && contains(inter, 4));

// list_difference
let diff = list_difference(a, b);
test("list_difference finds unique to first", len(diff) == 2 && contains(diff, 1) && contains(diff, 2));

// list_union
let union = list_union(a, b);
test("list_union combines unique elements", len(union) == 6 && contains(union, 1) && contains(union, 6));

// list_partition
fn is_even(x) { return x % 2 == 0; }
let to_partition = [1, 2, 3, 4, 5, 6];
let partitioned = list_partition(to_partition, is_even);
test("list_partition splits correctly", len(partitioned) == 2);
test("list_partition truthy correct", len(partitioned[0]) == 3);
test("list_partition falsy correct", len(partitioned[1]) == 3);

// take
let take_test = [1, 2, 3, 4, 5];
let taken = take(take_test, 3);
test("take first n elements", len(taken) == 3 && taken[0] == 1 && taken[2] == 3);
test("take more than available", len(take(take_test, 10)) == 5);
test("take zero", len(take(take_test, 0)) == 0);

// drop
let dropped = drop(take_test, 2);
test("drop first n elements", len(dropped) == 3 && dropped[0] == 3 && dropped[2] == 5);
test("drop all", len(drop(take_test, 10)) == 0);
test("drop zero", len(drop(take_test, 0)) == 5);

// ===== Random Utilities =====
print("\n--- Random Utilities ---");

// random_int
let r1 = random_int(1, 10);
test("random_int in range", r1 >= 1 && r1 <= 10);

let r2 = random_int(5, 5);
test("random_int single value", r2 == 5);

// random_choice
let choices = ["apple", "banana", "cherry"];
let choice = random_choice(choices);
test("random_choice returns element", contains(choices, choice));

// shuffle (just test it doesn't crash and maintains length)
let to_shuffle = [1, 2, 3, 4, 5];
shuffle(to_shuffle);
test("shuffle maintains length", len(to_shuffle) == 5);
test("shuffle maintains elements", contains(to_shuffle, 1) && contains(to_shuffle, 5));

// ===== Advanced Features =====
print("\n--- Advanced Features ---");

// copy vs deepcopy
let original = [1, [2, 3], 4];
let shallow = copy(original);
let deep = deepcopy(original);

// Modify nested list
original[1][0] = 999;

test("shallow copy shares nested", shallow[1][0] == 999);
test("deepcopy independent", deep[1][0] == 2);

// Map copy
let map1 = map();
mset(map1, "a", 1);
mset(map1, "b", 2);
let map2 = copy(map1);
test("map copy works", mget(map2, "a") == 1 && mget(map2, "b") == 2);

// Set copy
let set1 = set();
set_add(set1, 1);
set_add(set1, 2);
let set2 = copy(set1);
test("set copy works", set_has(set2, 1) && set_has(set2, 2));

// contains
test("contains list", contains([1, 2, 3], 2));
test("contains list negative", !contains([1, 2, 3], 5));
test("contains string", contains("hello world", "world"));
test("contains string negative", !contains("hello world", "xyz"));

let test_map = map();
mset(test_map, "key", "value");
test("contains map", contains(test_map, "key"));

let test_set = set();
set_add(test_set, "item");
test("contains set", contains(test_set, "item"));

// reverse and reversed
let orig_list = [1, 2, 3, 4];
let rev_copy = reversed(orig_list);
test("reversed creates new list", rev_copy[0] == 4 && orig_list[0] == 1);

reverse(orig_list);
test("reverse modifies in place", orig_list[0] == 4 && orig_list[3] == 1);

print("\n=== Test Results ===");
print("Passed: " + to_str(passed));
print("Failed: " + to_str(failed));
print("Total:  " + to_str(passed + failed));

if (failed == 0) {
    print("\n✓ All tests passed!");
} else {
    print("\n✗ Some tests failed");
}
