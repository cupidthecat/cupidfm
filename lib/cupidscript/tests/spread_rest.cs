// Spread and rest

fn sum(...items) {
    let total = 0;
    for i in items {
        total = total + i;
    }
    return total;
}

let xs = [1, 2, 3];
assert(sum(...xs) == 6, "spread in call");

let ys = [0, ...xs, 4];
assert(ys[0] == 0, "spread list start");
assert(ys[3] == 3, "spread list mid");
assert(ys[4] == 4, "spread list end");

let m1 = {a: 1, b: 2};
let m2 = {b: 3, c: 4};
let m3 = {...m1, ...m2};
assert(m3["a"] == 1, "spread map a");
assert(m3["b"] == 3, "spread map override");
assert(m3["c"] == 4, "spread map c");

let [a, b, ...rest] = [1, 2, 3, 4];
assert(a == 1, "rest a");
assert(b == 2, "rest b");
assert(rest[0] == 3, "rest list 0");
assert(rest[1] == 4, "rest list 1");

let {a: x, ...other} = {a: 1, b: 2, c: 3};
assert(x == 1, "rest map a");
assert(other["b"] == 2, "rest map b");
assert(other["c"] == 3, "rest map c");

print("spread_rest ok");
