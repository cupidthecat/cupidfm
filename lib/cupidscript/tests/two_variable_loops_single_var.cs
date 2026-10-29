// Test that single variable for-in loops still work

// Single variable - list
for val in [10, 20, 30] {
    print("val:", val);
}

// Single variable - map
for key in {"a": 1, "b": 2} {
    print("key:", key);
}

print("Single variable test complete");
