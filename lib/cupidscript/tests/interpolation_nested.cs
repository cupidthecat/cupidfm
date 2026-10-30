fn inner(value) { return "inner:${value}"; }
assert("outer:${inner(7)}:tail" == "outer:inner:7:tail", "nested interpolation");
assert("a${inner(1)}b${inner(2)}c" == "ainner:1binner:2c", "repeated nested interpolation");
fn fail() { throw "stop"; }
try { let ignored = "prefix:${fail()}:suffix"; } catch (error) { assert(error == "stop", "throw preserved"); }
assert("after:${inner(3)}" == "after:inner:3", "cache restored after failure");
