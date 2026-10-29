// Test suite for math functions

// Basic Math Functions
assert(abs(-5) == 5, "abs: negative int");
assert(abs(5) == 5, "abs: positive int");
assert(abs(-3.14) > 3.13 && abs(-3.14) < 3.15, "abs: negative float");
assert(abs(0) == 0, "abs: zero");

assert(min(3, 7, 2) == 2, "min: multiple values");
assert(min(1, 2) == 1, "min: two values");
assert(min(5) == 5, "min: single value");
assert(min(-10, 0, 10) == -10, "min: with negatives");
assert(min(1.5, 2.5, 0.5) > 0.4 && min(1.5, 2.5, 0.5) < 0.6, "min: floats");

assert(max(3, 7, 2) == 7, "max: multiple values");
assert(max(1, 2) == 2, "max: two values");
assert(max(5) == 5, "max: single value");
assert(max(-10, 0, 10) == 10, "max: with negatives");
assert(max(1.5, 2.5, 3.5) > 3.4 && max(1.5, 2.5, 3.5) < 3.6, "max: floats");

assert(clamp(5, 0, 10) == 5, "clamp: within range");
assert(clamp(-5, 0, 10) == 0, "clamp: below min");
assert(clamp(15, 0, 10) == 10, "clamp: above max");
assert(clamp(3, 0, 10) == 3, "clamp: at min boundary");
assert(clamp(10, 0, 10) == 10, "clamp: at max boundary");

// Rounding Functions
assert(floor(3.7) == 3, "floor: positive");
assert(floor(-3.7) == -4, "floor: negative");
assert(floor(5) == 5, "floor: integer");
assert(floor(0.1) == 0, "floor: small positive");
assert(floor(-0.1) == -1, "floor: small negative");

assert(ceil(3.2) == 4, "ceil: positive");
assert(ceil(-3.2) == -3, "ceil: negative");
assert(ceil(5) == 5, "ceil: integer");
assert(ceil(0.1) == 1, "ceil: small positive");
assert(ceil(-0.1) == 0, "ceil: small negative");

assert(round(3.7) == 4, "round: up");
assert(round(3.2) == 3, "round: down");
assert(round(3.5) == 4, "round: half up positive");
assert(round(-3.5) == -4, "round: half up negative");
assert(round(5) == 5, "round: integer");

assert(trunc(3.7) == 3, "trunc: positive");
assert(trunc(-3.7) == -3, "trunc: negative");
assert(trunc(5) == 5, "trunc: integer");
assert(trunc(0.9) == 0, "trunc: small positive");
assert(trunc(-0.9) == 0, "trunc: small negative");

// Powers and Roots
assert(sqrt(16) == 4.0, "sqrt: perfect square");
assert(sqrt(0) == 0.0, "sqrt: zero");
assert(sqrt(1) == 1.0, "sqrt: one");
let sqrt2 = sqrt(2);
assert(sqrt2 > 1.41 && sqrt2 < 1.42, "sqrt: irrational");

assert(pow(2, 8) == 256.0, "pow: positive exponent");
assert(pow(10, 3) == 1000.0, "pow: base 10");
assert(pow(2, 0) == 1.0, "pow: zero exponent");
assert(pow(5, 1) == 5.0, "pow: one exponent");
let pow_half = pow(4, 0.5);
assert(pow_half > 1.99 && pow_half < 2.01, "pow: fractional exponent");

let e_val = exp(1);
assert(e_val > 2.71 && e_val < 2.72, "exp: e^1");
assert(exp(0) == 1.0, "exp: e^0");
let exp2 = exp(2);
assert(exp2 > 7.38 && exp2 < 7.39, "exp: e^2");

// Logarithms
let ln_e = log(E);
assert(ln_e > 0.99 && ln_e < 1.01, "log: ln(e)");
assert(log(1) == 0.0, "log: ln(1)");
let ln10 = log(10);
assert(ln10 > 2.30 && ln10 < 2.31, "log: ln(10)");

assert(log10(100) == 2.0, "log10: 100");
assert(log10(1000) == 3.0, "log10: 1000");
assert(log10(1) == 0.0, "log10: 1");
assert(log10(10) == 1.0, "log10: 10");

assert(log2(8) == 3.0, "log2: 8");
assert(log2(1024) == 10.0, "log2: 1024");
assert(log2(1) == 0.0, "log2: 1");
assert(log2(2) == 1.0, "log2: 2");
assert(log2(16) == 4.0, "log2: 16");

// Trigonometry (sin, cos, tan)
assert(sin(0) == 0.0, "sin: 0");
let sin_pi_2 = sin(PI / 2);
assert(sin_pi_2 > 0.99 && sin_pi_2 < 1.01, "sin: PI/2");
let sin_pi = sin(PI);
assert(sin_pi > -0.01 && sin_pi < 0.01, "sin: PI");

assert(cos(0) == 1.0, "cos: 0");
let cos_pi_2 = cos(PI / 2);
assert(cos_pi_2 > -0.01 && cos_pi_2 < 0.01, "cos: PI/2");
let cos_pi = cos(PI);
assert(cos_pi > -1.01 && cos_pi < -0.99, "cos: PI");

assert(tan(0) == 0.0, "tan: 0");
let tan_pi_4 = tan(PI / 4);
assert(tan_pi_4 > 0.99 && tan_pi_4 < 1.01, "tan: PI/4");

// Inverse trigonometry
assert(asin(0) == 0.0, "asin: 0");
let asin_1 = asin(1);
assert(asin_1 > 1.57 && asin_1 < 1.58, "asin: 1 (PI/2)");
let asin_half = asin(0.5);
assert(asin_half > 0.52 && asin_half < 0.53, "asin: 0.5 (PI/6)");

let acos_1 = acos(1);
assert(acos_1 == 0.0, "acos: 1");
let acos_0 = acos(0);
assert(acos_0 > 1.57 && acos_0 < 1.58, "acos: 0 (PI/2)");
let acos_neg1 = acos(-1);
assert(acos_neg1 > 3.14 && acos_neg1 < 3.15, "acos: -1 (PI)");

assert(atan(0) == 0.0, "atan: 0");
let atan_1 = atan(1);
assert(atan_1 > 0.78 && atan_1 < 0.79, "atan: 1 (PI/4)");
let atan_neg1 = atan(-1);
assert(atan_neg1 > -0.79 && atan_neg1 < -0.78, "atan: -1");

// atan2 tests
let atan2_1_1 = atan2(1, 1);
assert(atan2_1_1 > 0.78 && atan2_1_1 < 0.79, "atan2: (1,1) = PI/4");
let atan2_1_0 = atan2(1, 0);
assert(atan2_1_0 > 1.57 && atan2_1_0 < 1.58, "atan2: (1,0) = PI/2");
let atan2_0_neg1 = atan2(0, -1);
assert(atan2_0_neg1 > 3.14 && atan2_0_neg1 < 3.15, "atan2: (0,-1) = PI");
let atan2_neg1_0 = atan2(-1, 0);
assert(atan2_neg1_0 > -1.58 && atan2_neg1_0 < -1.57, "atan2: (-1,0) = -PI/2");

// Random Functions
let r1 = random();
assert(r1 >= 0.0 && r1 < 1.0, "random: in range [0, 1)");

let ri1 = random_int(1, 6);
assert(ri1 >= 1 && ri1 <= 6, "random_int: dice roll");

let ri2 = random_int(10, 1);
assert(ri2 >= 1 && ri2 <= 10, "random_int: swapped args");

let ri3 = random_int(-5, 5);
assert(ri3 >= -5 && ri3 <= 5, "random_int: negative range");

// random_choice tests
let choices = ["a", "b", "c", "d", "e"];
let choice = random_choice(choices);
assert(choice == "a" || choice == "b" || choice == "c" || choice == "d" || choice == "e", 
       "random_choice: valid element");

let empty_list = [];
let nil_choice = random_choice(empty_list);
assert(nil_choice == nil, "random_choice: empty list");

let single = [42];
let single_choice = random_choice(single);
assert(single_choice == 42, "random_choice: single element");

// shuffle test
let deck = [1, 2, 3, 4, 5];
let original = [1, 2, 3, 4, 5];
shuffle(deck);
// Check all elements still present
let found = [false, false, false, false, false];
for i in range(5) {
    for j in range(5) {
        if (deck[i] == original[j]) {
            found[j] = true;
        }
    }
}
assert(found[0] && found[1] && found[2] && found[3] && found[4], 
       "shuffle: all elements present");

// Test with single element (should not error)
let single_list = [1];
shuffle(single_list);
assert(single_list[0] == 1, "shuffle: single element");

// Test mathematical identities
let a = 3.7;
let b = 2.1;

// sin²(x) + cos²(x) = 1
let sin_a = sin(a);
let cos_a = cos(a);
let identity = sin_a * sin_a + cos_a * cos_a;
assert(identity > 0.99 && identity < 1.01, "trig identity: sin²+cos²=1");

// log(x*y) = log(x) + log(y)
let log_product = log(a * b);
let sum_logs = log(a) + log(b);
assert(log_product > sum_logs - 0.01 && log_product < sum_logs + 0.01, 
       "log identity: log(xy)=log(x)+log(y)");

// e^(log(x)) = x
let x = 5.3;
let elog = exp(log(x));
assert(elog > x - 0.01 && elog < x + 0.01, "exp/log inverse");

// Practical examples
fn distance(x1, y1, x2, y2) {
    let dx = x2 - x1;
    let dy = y2 - y1;
    return sqrt(dx * dx + dy * dy);
}

let dist = distance(0, 0, 3, 4);
assert(dist > 4.99 && dist < 5.01, "distance formula: 3-4-5 triangle");

fn deg_to_rad(deg) {
    return deg * PI / 180;
}

fn rad_to_deg(rad) {
    return rad * 180 / PI;
}

let angle_rad = deg_to_rad(90);
assert(angle_rad > 1.57 && angle_rad < 1.58, "deg to rad: 90°");

let angle_deg = rad_to_deg(PI);
assert(angle_deg > 179 && angle_deg < 181, "rad to deg: π");

// Polar to Cartesian
fn polar_to_cart(r, theta) {
    return {x: r * cos(theta), y: r * sin(theta)};
}

let point = polar_to_cart(1, PI / 4);
assert(point.x > 0.70 && point.x < 0.72, "polar to cart: x");
assert(point.y > 0.70 && point.y < 0.72, "polar to cart: y");

// Cartesian to Polar
fn cart_to_polar(x, y) {
    let r = sqrt(x * x + y * y);
    let theta = atan2(y, x);
    return {r: r, theta: theta};
}

let polar = cart_to_polar(1, 1);
assert(polar.r > 1.41 && polar.r < 1.42, "cart to polar: r");
assert(polar.theta > 0.78 && polar.theta < 0.79, "cart to polar: theta");

print("test_math.cs: All math tests passed!");
