// Test two-variable iteration for lists

let numbers = [10, 20, 30];

// Test comma syntax
print("Test 1: Comma syntax");
for val, idx in numbers {
    assert(val == numbers[idx], "value matches at index");
    print("  idx:", idx, "val:", val);
}

// Test destructuring syntax
print("Test 2: Destructuring syntax");
for [val, idx] in numbers {
    assert(val == numbers[idx], "value matches at index");
    print("  idx:", idx, "val:", val);
}

// Test single variable still works
print("Test 3: Single variable (backward compat)");
let count = 0;
for val in numbers {
    assert(val == numbers[count], "single var still works");
    count = count + 1;
}

print("List tests passed");
