// EXPECT_FAIL
// require_optional() should still propagate parse/runtime errors when file exists

let x = require_optional("modules/_bad.cs");
print(x);
