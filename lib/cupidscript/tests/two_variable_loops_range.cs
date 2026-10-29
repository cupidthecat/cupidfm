// Test two-variable iteration for ranges

fn assert_equal(actual, expected, test_name) {
    if (actual != expected) {
        print("FAIL:", test_name, "- expected", expected, "but got", actual);
        throw "assertion failed";
    }
}

// Test comma syntax (num, count)
print("Test 1: Comma syntax - positive step");
for num, i in 5..8 {
    assert_equal(num, 5 + i, "number matches offset from start");
    print("  i:", i, "num:", num);
}

// Test destructuring syntax
print("Test 2: Destructuring syntax");
let expected_i = 0;
for [num, i] in 10..13 {
    assert_equal(i, expected_i, "iteration count matches");
    expected_i = expected_i + 1;
}

// Test negative step
print("Test 3: Negative step range");
let count = 0;
for num, i in 8..5 {
    assert_equal(i, count, "count increments even with negative step");
    count = count + 1;
}

// Test single variable still works
print("Test 4: Single variable (backward compat)");
let sum = 0;
for num in 1..4 {
    sum = sum + num;
}
assert_equal(sum, 6, "single var sums correctly: 1+2+3");

print("Range tests passed");
