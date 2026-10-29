// EXPECT_FAIL

fn broken() {
  let x = 1;
  if (x) {
    x = x + 1;
  }
  // Missing closing brace for function body
