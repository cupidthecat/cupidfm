// Comprehensive two-variable loop tests

print("=== Two-Variable Loop Integration Tests ===\n");

// Lists
print("Test 1: Lists with both syntaxes");
let numbers = [10, 20, 30];

// Test comma syntax
let comma_sum = 0;
for val, idx in numbers {
    assert(val, numbers[idx], "comma syntax matches");
    comma_sum = comma_sum + val;
}

// Test destructuring syntax
let destruct_sum = 0;
for [val, idx] in numbers {
    assert(val, numbers[idx], "destructuring syntax matches");
    destruct_sum = destruct_sum + val;
}

assert(comma_sum, destruct_sum, "both syntaxes give same results");
print("  Lists: PASS\n");

// Maps
print("Test 2: Maps with comprehension consistency");
let data = {"x": 1, "y": 2, "z": 3};

// Test that for-loop matches comprehension
let loop_map = {};
for key, val in data {
    loop_map[key] = val;
}

let comp_map = {k: v for k, v in data};

for key in data {
    assert(loop_map[key], comp_map[key], "loop matches comprehension");
}
print("  Maps: PASS\n");

// Sets
print("Test 3: Sets track iteration order");
let items = set([100, 200, 300]);
let set_indices = [];

for val, i in items {
    push(set_indices, i);
}

assert(len(set_indices), 3, "set has 3 iterations");
assert(set_indices[0], 0, "first iteration is 0");
assert(set_indices[1], 1, "second iteration is 1");
assert(set_indices[2], 2, "third iteration is 2");
print("  Sets: PASS\n");

// Ranges
print("Test 4: Ranges with positive and negative steps");
let range_count = 0;
for num, i in 5..8 {
    assert(i, range_count, "range count matches");
    range_count = range_count + 1;
}

let neg_count = 0;
for num, i in 10..7 {
    assert(i, neg_count, "negative range count matches");
    neg_count = neg_count + 1;
}
print("  Ranges: PASS\n");

// Backward compatibility
print("Test 5: Single-variable loops still work");
let single_sum = 0;
for num in [1, 2, 3, 4] {
    single_sum = single_sum + num;
}
assert(single_sum, 10, "single var lists work");

let key_count = 0;
for key in {"a": 1, "b": 2} {
    key_count = key_count + 1;
}
assert(key_count, 2, "single var maps work");
print("  Backward compatibility: PASS\n");

// Edge cases
print("Test 6: Edge cases");

// Empty list
let empty_iterations = 0;
for val, idx in [] {
    empty_iterations = empty_iterations + 1;
}
assert(empty_iterations, 0, "empty list has no iterations");

// Single element
for val, idx in [42] {
    assert(idx, 0, "single element has index 0");
    assert(val, 42, "single element has correct value");
}
print("  Edge cases: PASS\n");

// Nested loops
print("Test 7: Nested two-variable loops");
let matrix = [[1, 2], [3, 4]];
let nested_results = [];

for row, i in matrix {
    for val, j in row {
        push(nested_results, {i: i, j: j, val: val});
    }
}

assert(len(nested_results), 4, "nested loops iterate correctly");
assert(nested_results[0]["i"], 0, "outer index correct");
assert(nested_results[0]["j"], 0, "inner index correct");
assert(nested_results[0]["val"], 1, "nested value correct");
print("  Nested loops: PASS\n");

print("=== All Integration Tests Passed ===");
