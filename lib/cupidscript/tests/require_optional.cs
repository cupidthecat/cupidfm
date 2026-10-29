// require_optional() branches: missing file -> nil, existing file -> exports

let missing = require_optional("./__definitely_missing__.cs");
assert(is_nil(missing), "require_optional missing -> nil");

let a = require_optional("./modules/_lib.cs");
let b = require_optional("./modules/_lib.cs");
assert(!is_nil(a), "require_optional existing");
assert(is_map(a), "require_optional returns exports map");
assert(a == b, "require_optional uses module cache");

assert(a.inc() == 1, "module state 1");
assert(b.inc() == 2, "module state 2");

print("require_optional ok");
