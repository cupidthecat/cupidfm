// Stdlib math: sin/cos/tan/log/exp/random + PI/E

fn approx(a, b, eps) {
  return abs(a - b) < eps;
}

assert(approx(PI, 3.14159265, 1e-6), "PI constant");
assert(approx(E, 2.71828182, 1e-6), "E constant");

assert(approx(sin(0), 0, 1e-9), "sin(0)");
assert(approx(cos(0), 1, 1e-9), "cos(0)");
assert(approx(tan(0), 0, 1e-9), "tan(0)");

assert(approx(log(1), 0, 1e-9), "log(1)");
assert(approx(exp(0), 1, 1e-9), "exp(0)");
assert(approx(sin(PI / 2), 1, 1e-6), "sin(PI/2)");
assert(approx(log(E), 1, 1e-6), "log(E)");
assert(approx(exp(1), E, 1e-6), "exp(1)");

let r = random();
assert(typeof(r) == "float", "random type");
assert(r >= 0 && r < 1, "random range");

print("stdlib_math_more ok");
