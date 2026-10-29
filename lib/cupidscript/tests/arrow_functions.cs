// Arrow functions

fn add(a, b) => a + b;

let inc = fn(x) => x + 1;
let mul = fn(x, factor = 2) => x * factor;
let block = fn(x) => {
    let y = x * 2;
    return y + 1;
};

assert(add(2, 3) == 5, "named arrow");
assert(inc(4) == 5, "anon arrow");
assert(mul(3) == 6, "arrow default");
assert(mul(3, 3) == 9, "arrow explicit");
assert(block(3) == 7, "arrow block");

print("arrow_functions ok");
