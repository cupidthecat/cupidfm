// Test that original comma syntax still works

// Lists - comma syntax
for val, idx in [10, 20, 30] {
    print("val:", val, "idx:", idx);
}

// Maps - comma syntax
for key, val in {"a": 1, "b": 2} {
    print("key:", key, "val:", val);
}

print("Comma syntax test complete");
