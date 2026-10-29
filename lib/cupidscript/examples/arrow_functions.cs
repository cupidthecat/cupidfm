// Arrow function examples

fn add(a, b) => a + b;
let inc = fn(x) => x + 1;
let scale = fn(x, factor = 2) => x * factor;

print(add(2, 3));
print(inc(10));
print(scale(4));
print(scale(4, 3));

let block = fn(x) => {
    let y = x * 2;
    return y + 1;
};
print(block(5));
