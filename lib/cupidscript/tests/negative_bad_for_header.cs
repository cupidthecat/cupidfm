// EXPECT_FAIL
// Bad C-style for header (missing semicolon)

let i = 0;
for (i = 0 i < 3; i = i + 1) {
  print(i);
}
