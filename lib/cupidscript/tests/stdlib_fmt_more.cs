// fmt: %b / %s / %v / %%

assert(fmt("%b %b", true, false) == "true false", "%b");
assert(fmt("%s", "hi") == "hi", "%s");

// %v is value repr; just check it returns a string
let v = fmt("%v", [1, 2]);
assert(is_string(v), "%v returns string");

assert(fmt("100%%") == "100%", "%% escape");

print("stdlib_fmt_more ok");
