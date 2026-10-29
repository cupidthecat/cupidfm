// return without value yields nil

fn f() { return; }
let v = f();
assert(is_nil(v), "return; produces nil");

print("return_nil ok");
