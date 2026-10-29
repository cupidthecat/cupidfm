// export statement tests

let lib = require("_exports_lib.cs");
assert(lib.value == 42, "export value");
assert(lib.greet("frank") == "hi frank", "export function");

print("exports ok");
