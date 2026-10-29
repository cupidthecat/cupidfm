// Unit tests for enumerate() function and list iteration order
// Tests verify correctness of both approaches

let test_count = 0;
let pass_count = 0;
let fail_count = 0;

fn assert_equal(actual, expected, test_name) {
    test_count = test_count + 1;
    if (actual == expected) {
        pass_count = pass_count + 1;
        print("✓ PASS:", test_name);
    } else {
        fail_count = fail_count + 1;
        print("✗ FAIL:", test_name);
        print("  Expected:", expected);
        print("  Actual:  ", actual);
    }
}

fn assert_true(condition, test_name) {
    test_count = test_count + 1;
    if (condition) {
        pass_count = pass_count + 1;
        print("✓ PASS:", test_name);
    } else {
        fail_count = fail_count + 1;
        print("✗ FAIL:", test_name, "(expected true, got false)");
    }
}

print("=== ENUMERATE() UNIT TESTS ===\n");

// Test 1: Basic enumerate with simple list
print("Test Group 1: Basic enumerate() functionality");
let basic = ["a", "b", "c"];
let enum_basic = enumerate(basic);
assert_equal(len(enum_basic), 3, "enumerate() returns list of correct length");
assert_equal(enum_basic[0][0], 0, "First element index is 0");
assert_equal(enum_basic[0][1], "a", "First element value is 'a'");
assert_equal(enum_basic[1][0], 1, "Second element index is 1");
assert_equal(enum_basic[1][1], "b", "Second element value is 'b'");
assert_equal(enum_basic[2][0], 2, "Third element index is 2");
assert_equal(enum_basic[2][1], "c", "Third element value is 'c'");
print();

// Test 2: enumerate() with empty list
print("Test Group 2: Edge cases");
let empty_enum = enumerate([]);
assert_equal(len(empty_enum), 0, "enumerate([]) returns empty list");

let single_enum = enumerate(["only"]);
assert_equal(len(single_enum), 1, "enumerate() with single element returns length 1");
assert_equal(single_enum[0][0], 0, "Single element has index 0");
assert_equal(single_enum[0][1], "only", "Single element has correct value");
print();

// Test 3: enumerate() with different types
print("Test Group 3: Different value types");
let num_list = [10, 20, 30];
let enum_nums = enumerate(num_list);
assert_equal(enum_nums[0][1], 10, "enumerate() works with integers");
assert_equal(enum_nums[1][1], 20, "enumerate() preserves integer values");

let mixed = [1, "two", 3.0, nil, true];
let enum_mixed = enumerate(mixed);
assert_equal(enum_mixed[0][1], 1, "enumerate() works with mixed types - int");
assert_equal(enum_mixed[1][1], "two", "enumerate() works with mixed types - string");
assert_equal(enum_mixed[2][1], 3.0, "enumerate() works with mixed types - float");
assert_equal(enum_mixed[3][1], nil, "enumerate() works with mixed types - nil");
assert_equal(enum_mixed[4][1], true, "enumerate() works with mixed types - bool");
print();

// Test 4: Using enumerate in comprehensions
print("Test Group 4: enumerate() in comprehensions");
let items = ["x", "y", "z"];

let indexed_map = {idx: val for [idx, val] in enumerate(items)};
assert_equal(indexed_map[0], "x", "Map comprehension with enumerate - index 0");
assert_equal(indexed_map[1], "y", "Map comprehension with enumerate - index 1");
assert_equal(indexed_map[2], "z", "Map comprehension with enumerate - index 2");

let labeled_list = [to_str(i) + ":" + v for [i, v] in enumerate(items)];
assert_equal(labeled_list[0], "0:x", "List comprehension with enumerate - first");
assert_equal(labeled_list[1], "1:y", "List comprehension with enumerate - second");
assert_equal(labeled_list[2], "2:z", "List comprehension with enumerate - third");
print();

// Test 5: Native iteration order (value, index)
print("Test Group 5: Native iteration order verification");
let data = ["A", "B", "C"];

// Collect using native syntax
let native_map = {idx: val for val, idx in data};
assert_equal(native_map[0], "A", "Native syntax: {idx: val for val, idx} - index 0");
assert_equal(native_map[1], "B", "Native syntax: {idx: val for val, idx} - index 1");
assert_equal(native_map[2], "C", "Native syntax: {idx: val for val, idx} - index 2");

// Verify first variable is VALUE
let first_vars = [v for v, i in data];
assert_equal(first_vars[0], "A", "Native syntax: first variable is VALUE - element 0");
assert_equal(first_vars[1], "B", "Native syntax: first variable is VALUE - element 1");
assert_equal(first_vars[2], "C", "Native syntax: first variable is VALUE - element 2");

// Verify second variable is INDEX
let second_vars = [i for v, i in data];
assert_equal(second_vars[0], 0, "Native syntax: second variable is INDEX - element 0");
assert_equal(second_vars[1], 1, "Native syntax: second variable is INDEX - element 1");
assert_equal(second_vars[2], 2, "Native syntax: second variable is INDEX - element 2");
print();

// Test 6: Comparing both approaches produce same results
print("Test Group 6: Equivalence tests");
let test_list = [100, 200, 300];

let native_result = {i: v for v, i in test_list};
let enum_result = {i: v for [i, v] in enumerate(test_list)};

assert_equal(native_result[0], enum_result[0], "Both methods produce same result - index 0");
assert_equal(native_result[1], enum_result[1], "Both methods produce same result - index 1");
assert_equal(native_result[2], enum_result[2], "Both methods produce same result - index 2");
print();

// Test 7: Filtering with enumerate
print("Test Group 7: Filtering with enumerate");
let numbers = [10, 15, 20, 25, 30];

let even_indices = [v for [i, v] in enumerate(numbers) if i % 2 == 0];
assert_equal(len(even_indices), 3, "Filter by even index - correct count");
assert_equal(even_indices[0], 10, "Filter by even index - first (idx 0)");
assert_equal(even_indices[1], 20, "Filter by even index - second (idx 2)");
assert_equal(even_indices[2], 30, "Filter by even index - third (idx 4)");

let odd_indices = [v for [i, v] in enumerate(numbers) if i % 2 != 0];
assert_equal(len(odd_indices), 2, "Filter by odd index - correct count");
assert_equal(odd_indices[0], 15, "Filter by odd index - first (idx 1)");
assert_equal(odd_indices[1], 25, "Filter by odd index - second (idx 3)");
print();

// Test 8: Nested enumerate
print("Test Group 8: Nested enumerate");
let matrix = [
    [1, 2],
    [3, 4]
];

let flattened = [];
for pair1 in enumerate(matrix) {
    let i = pair1[0];
    let row = pair1[1];
    for pair2 in enumerate(row) {
        let j = pair2[0];
        let val = pair2[1];
        push(flattened, [i, j, val]);
    }
}

assert_equal(len(flattened), 4, "Nested enumerate - correct element count");
assert_equal(flattened[0][0], 0, "Nested enumerate - [0,0] row index");
assert_equal(flattened[0][1], 0, "Nested enumerate - [0,0] col index");
assert_equal(flattened[0][2], 1, "Nested enumerate - [0,0] value");
assert_equal(flattened[3][0], 1, "Nested enumerate - [1,1] row index");
assert_equal(flattened[3][1], 1, "Nested enumerate - [1,1] col index");
assert_equal(flattened[3][2], 4, "Nested enumerate - [1,1] value");
print();

// Test 9: enumerate() preserves order
print("Test Group 9: Order preservation");
let ordered = [5, 4, 3, 2, 1];
let enum_ordered = enumerate(ordered);

let indices_in_order = true;
for i in range(len(enum_ordered)) {
    if (enum_ordered[i][0] != i) {
        indices_in_order = false;
    }
}
assert_true(indices_in_order, "enumerate() preserves sequential index order");

let values_in_order = true;
for i in range(len(enum_ordered)) {
    if (enum_ordered[i][1] != ordered[i]) {
        values_in_order = false;
    }
}
assert_true(values_in_order, "enumerate() preserves value order");
print();

// Test 10: Large list performance test
print("Test Group 10: Large list handling");
let large_list = [i for i in range(100)];
let enum_large = enumerate(large_list);

assert_equal(len(enum_large), 100, "enumerate() handles large list - correct length");
assert_equal(enum_large[0][0], 0, "enumerate() large list - first index");
assert_equal(enum_large[0][1], 0, "enumerate() large list - first value");
assert_equal(enum_large[99][0], 99, "enumerate() large list - last index");
assert_equal(enum_large[99][1], 99, "enumerate() large list - last value");
assert_equal(enum_large[50][0], 50, "enumerate() large list - middle index");
assert_equal(enum_large[50][1], 50, "enumerate() large list - middle value");
print();

// Test 11: Creating reverse lookups
print("Test Group 11: Practical use cases");
let names = ["Alice", "Bob", "Charlie"];

let name_to_id = {name: id for [id, name] in enumerate(names)};
assert_equal(name_to_id["Alice"], 0, "Reverse lookup - Alice");
assert_equal(name_to_id["Bob"], 1, "Reverse lookup - Bob");
assert_equal(name_to_id["Charlie"], 2, "Reverse lookup - Charlie");

let id_to_name = {id: name for [id, name] in enumerate(names)};
assert_equal(id_to_name[0], "Alice", "Forward lookup - 0");
assert_equal(id_to_name[1], "Bob", "Forward lookup - 1");
assert_equal(id_to_name[2], "Charlie", "Forward lookup - 2");
print();

// Test 12: enumerate() with for loops
print("Test Group 12: enumerate() in for loops");
let loop_data = ["p", "q", "r"];
let collected_indices = [];
let collected_values = [];

for pair in enumerate(loop_data) {
    let idx = pair[0];
    let val = pair[1];
    push(collected_indices, idx);
    push(collected_values, val);
}

assert_equal(len(collected_indices), 3, "For loop with enumerate - collected all indices");
assert_equal(len(collected_values), 3, "For loop with enumerate - collected all values");
assert_equal(collected_indices[0], 0, "For loop enumerate - index 0");
assert_equal(collected_indices[1], 1, "For loop enumerate - index 1");
assert_equal(collected_indices[2], 2, "For loop enumerate - index 2");
assert_equal(collected_values[0], "p", "For loop enumerate - value 0");
assert_equal(collected_values[1], "q", "For loop enumerate - value 1");
assert_equal(collected_values[2], "r", "For loop enumerate - value 2");
print();

// Print summary
print("=== TEST SUMMARY ===");
print("Total tests:  ", test_count);
print("Passed:       ", pass_count);
print("Failed:       ", fail_count);
print("Pass rate:    ", (pass_count * 100 / test_count), "%");

if (fail_count == 0) {
    print("\n✓ ALL TESTS PASSED!");
} else {
    print("\n✗ SOME TESTS FAILED!");
}
