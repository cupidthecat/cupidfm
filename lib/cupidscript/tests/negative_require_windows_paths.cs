// EXPECT_FAIL

// Force path_normalize drive/UNC branches in cs_vm.c
require("C:\\nonexistent\\nope.cs");
require("//server/share/nope.cs");
