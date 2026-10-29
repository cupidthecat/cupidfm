// Test that destructuring syntax parses without errors

// Lists - destructuring syntax
for [val, idx] in [10, 20, 30] {
    print("val:", val, "idx:", idx);
}

// Maps - destructuring syntax
for [key, val] in {"a": 1, "b": 2} {
    print("key:", key, "val:", val);
}

print("Parser test complete");
