// EXPECT_FAIL
// Non-default parameter after default parameter should fail

fn bad(a = 1, b) {
    return a + b;
}
