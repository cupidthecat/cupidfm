// Exercise fmt() branches: literal percent and %v representations.

assert(fmt("%%") == "%", "fmt literal percent");
assert(fmt("x=%d %%", 3) == "x=3 %", "fmt mixed percent");

let m = {"a": 1};
let l = [1, 2];

let mv = fmt("%v", m);
let lv = fmt("%v", l);

assert(str_startswith(mv, "<map len="), "fmt %v map");
assert(str_startswith(lv, "<list len="), "fmt %v list");

print("stdlib_fmt_percent_and_v ok");

