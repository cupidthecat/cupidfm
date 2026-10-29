// EXPECT_FAIL
// Function definition parameter parse error (cs_parser.c: expected parameter name)

fn bad(1) {
  return 0;
}

print(bad());
