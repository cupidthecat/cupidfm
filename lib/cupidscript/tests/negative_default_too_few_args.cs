// EXPECT_FAIL
// Calling with too few arguments (missing required params)

fn needs_one(a, b = 10) {
    return a + b;
}

// This should fail - 'a' has no default
needs_one();
