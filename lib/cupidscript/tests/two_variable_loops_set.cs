// Test two-variable iteration for sets

let items = set([100, 200, 300]);

// Test comma syntax (element, iteration_count)
print("Test 1: Comma syntax");
let max_i = -1;
for val, i in items {
    print("  i:", i, "val:", val);
    assert(typeof(i) == "int", "iteration count is int");
    assert(i >= 0 && i <= 2, "iteration count in range");
    if (i > max_i) { max_i = i; }
}
assert(max_i == 2, "iteration count reaches 2");

// Test destructuring syntax
print("Test 2: Destructuring syntax");
let count = 0;
for [val, i] in items {
    assert(typeof(i) == "int", "iteration count is int");
    assert(i == count, "iteration count increments");
    print("  i:", i, "val:", val);
    count = count + 1;
}

// Test single variable still works (gets elements)
print("Test 3: Single variable (backward compat)");
let found_100 = false;
let found_200 = false;
let found_300 = false;
for val in items {
    if (val == 100) { found_100 = true; }
    if (val == 200) { found_200 = true; }
    if (val == 300) { found_300 = true; }
}
assert(found_100 && found_200 && found_300, "all elements found");

print("Set tests passed");
