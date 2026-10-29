// EXPECT_FAIL
// Function literal parameter parse error (cs_parser.c: expected parameter name)

let f = fn(1) { return 0; };
print(f);
