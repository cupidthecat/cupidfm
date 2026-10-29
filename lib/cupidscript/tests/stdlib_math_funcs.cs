// Stdlib math: abs/min/max/floor/ceil/round/sqrt/pow

assert(abs(-3) == 3, "abs int");
let af = abs(-3.5);
assert(typeof(af) == "float", "abs float type");
assert(af > 3.4 && af < 3.6, "abs float value");

assert(min(3, 1, 2) == 1, "min");
assert(max(3, 1, 2) == 3, "max");

assert(floor(1.9) == 1, "floor");
assert(ceil(1.1) == 2, "ceil");
assert(round(1.6) == 2, "round");

let s = sqrt(9);
assert(typeof(s) == "float", "sqrt type");
assert(s > 2.9 && s < 3.1, "sqrt value");

let p = pow(2, 3);
assert(typeof(p) == "float", "pow type");
assert(p > 7.9 && p < 8.1, "pow value");

print("stdlib_math_funcs ok");
