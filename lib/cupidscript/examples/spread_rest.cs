// Spread and rest examples

fn log_all(prefix, ...items) {
    for item in items {
        print(prefix, item);
    }
}

let nums = [1, 2, 3];
log_all(">", ...nums);

let more = [0, ...nums, 4];
print(more);

let base = {a: 1, b: 2};
let extra = {b: 3, c: 4};
let merged = {...base, ...extra};
print(merged);

let [first, second, ...rest] = [1, 2, 3, 4, 5];
print(first, second, rest);

let {a, ...other} = {a: 1, b: 2, c: 3};
print(a, other);
