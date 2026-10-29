// require() caching should treat './' and '..' as same module key

let a = require("./modules/_lib.cs");
let b = require("./modules/./_lib.cs");
let c = require("./modules/x/../_lib.cs");
let d = require("./modules//_lib.cs");

// Same exports map instance means identity equality should hold.
assert(a == b, "require path normalization (./)");
assert(a == c, "require path normalization (..)");
assert(a == d, "require path normalization (//)");

assert(a.inc() == 1, "module state 1");
assert(b.inc() == 2, "module state 2");
assert(c.inc() == 3, "module state 3");
assert(d.inc() == 4, "module state 4");

print("modules ok");
