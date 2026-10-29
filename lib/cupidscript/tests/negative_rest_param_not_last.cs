// EXPECT_FAIL
// Rest parameter must be last

fn bad(...rest, a) {
    return a;
}
