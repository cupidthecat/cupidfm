// Test two-variable iteration for maps

fn assert_equal(actual, expected, test_name) {
    if (actual != expected) {
        print("FAIL:", test_name);
        print("  Expected:", expected);
        print("  Actual:", actual);
        throw "assertion failed";
    }
}

let data = {"a": 1, "b": 2, "c": 3};

// Test comma syntax (key, val)
print("Test 1: Comma syntax (key, val)");
for key, val in data {
    assert_equal(data[key], val, "value matches key");
    print("  key:", key, "val:", val);
}

// Test destructuring syntax
print("Test 2: Destructuring syntax");
for [key, val] in data {
    assert_equal(data[key], val, "value matches key");
    print("  key:", key, "val:", val);
}

// Test single variable still works (gets keys)
print("Test 3: Single variable (backward compat - gets keys)");
let found_a = false;
let found_b = false;
let found_c = false;
for key in data {
    if (key == "a") { found_a = true; }
    if (key == "b") { found_b = true; }
    if (key == "c") { found_c = true; }
}
assert_equal(found_a && found_b && found_c, true, "all keys found");

// Test consistency with comprehensions
print("Test 4: Comprehension consistency");
let comp_result = {k: v for k, v in data};
assert_equal(len(comp_result), 3, "comprehension works same way");

print("Map tests passed");
