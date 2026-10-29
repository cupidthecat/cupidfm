// EXPECT_FAIL
// Test that invalid destructuring patterns produce parse errors

// Missing second variable
for [a] in [1, 2, 3] {
    print(a);
}
