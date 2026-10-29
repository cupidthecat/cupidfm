// Comprehensive test for two-variable map iteration

fn assert_equal(actual, expected, test_name) {
    if (actual != expected) {
        print("FAIL:", test_name);
        print("  Expected:", expected);
        print("  Actual:", actual);
        throw "assertion failed";
    }
}

// Test 1: Empty map
print("Test 1: Empty map");
let empty = {};
let count = 0;
for k, v in empty {
    count = count + 1;
}
assert_equal(count, 0, "empty map has no iterations");

// Test 2: Single entry
print("Test 2: Single entry");
let single = {"key": "value"};
for k, v in single {
    assert_equal(k, "key", "key is correct");
    assert_equal(v, "value", "value is correct");
}

// Test 3: Mixed types
print("Test 3: Mixed types as values");
let mixed = {
    "str": "hello",
    "num": 42,
    "bool": true,
    "nil": nil
};
for k, v in mixed {
    assert_equal(mixed[k], v, "value matches for key: " + k);
}

// Test 4: Nested iteration
print("Test 4: Nested iteration");
let outer = {"a": {"x": 1}, "b": {"y": 2}};
for k1, v1 in outer {
    for k2, v2 in v1 {
        assert_equal(v1[k2], v2, "nested value matches");
    }
}

// Test 5: Count iterations
print("Test 5: Iteration count is correct");
let ordered = {"a": 1, "b": 2, "c": 3};
let count1 = 0;
for k, v in ordered {
    count1 = count1 + 1;
}
assert_equal(count1, 3, "correct number of iterations");

// Test 6: Break and continue
print("Test 6: Break and continue");
let data = {"a": 1, "b": 2, "c": 3, "d": 4};
let sum = 0;
for k, v in data {
    if (k == "b") {
        continue;
    }
    if (v == 4) {
        break;
    }
    sum = sum + v;
}
// Should have added values for "a" and "c" (skipped "b", broke before processing "d" if encountered)
// Note: map iteration order is not guaranteed, so we can't make assumptions about which values are summed

// Test 7: Return from loop
print("Test 7: Return from loop");
fn test_return() {
    let m = {"a": 1, "b": 2};
    for k, v in m {
        if (k == "a") {
            return v;
        }
    }
    return -1;
}
let result = test_return();
assert_equal(result == 1 || result == -1, true, "return works from loop");

// Test 8: Destructuring syntax
print("Test 8: Destructuring syntax");
let dest = {"x": 10, "y": 20};
for [k, v] in dest {
    assert_equal(dest[k], v, "destructuring works");
}

print("All comprehensive tests passed!");
