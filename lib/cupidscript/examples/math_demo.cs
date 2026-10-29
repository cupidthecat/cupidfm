// Math Functions Demo
// Demonstrates basic math, rounding, trigonometry, and random functions

println("=== Basic Math Functions ===");
println("abs(-42):", abs(-42));
println("min(5, 2, 8, 1):", min(5, 2, 8, 1));
println("max(5, 2, 8, 1):", max(5, 2, 8, 1));
println("clamp(15, 0, 10):", clamp(15, 0, 10));
println();

println("=== Rounding Functions ===");
let num = 3.7;
println("Number:", num);
println("floor(" + to_str(num) + "):", floor(num));
println("ceil(" + to_str(num) + "):", ceil(num));
println("round(" + to_str(num) + "):", round(num));
println("trunc(" + to_str(num) + "):", trunc(num));
println();

println("=== Powers and Roots ===");
println("sqrt(16):", sqrt(16));
println("pow(2, 8):", pow(2, 8));
println("exp(1):", exp(1));
println("log(E):", log(E));
println("log10(100):", log10(100));
println("log2(1024):", log2(1024));
println();

println("=== Trigonometry (angles in radians) ===");
println("sin(0):", sin(0));
println("sin(PI/2):", sin(PI / 2));
println("cos(0):", cos(0));
println("cos(PI):", cos(PI));
println("tan(PI/4):", tan(PI / 4));
println();

println("=== Inverse Trigonometry ===");
println("asin(0.5):", asin(0.5));
println("acos(0.5):", acos(0.5));
println("atan(1):", atan(1));
println("atan2(1, 1):", atan2(1, 1));
println();

println("=== Degree/Radian Conversion ===");
fn deg_to_rad(deg) { return deg * PI / 180; }
fn rad_to_deg(rad) { return rad * 180 / PI; }

println("90 degrees in radians:", deg_to_rad(90));
println("PI radians in degrees:", rad_to_deg(PI));
println();

println("=== Random Functions ===");
println("random():", random());
println("random():", random());
println("random_int(1, 6):", random_int(1, 6));
println("random_int(1, 6):", random_int(1, 6));

let colors = ["red", "green", "blue", "yellow"];
println("random_choice(colors):", random_choice(colors));
println("random_choice(colors):", random_choice(colors));

let deck = [1, 2, 3, 4, 5];
println("Original deck:", deck);
shuffle(deck);
println("Shuffled deck:", deck);
println();

println("=== Practical Examples ===");

// Distance formula
fn distance(x1, y1, x2, y2) {
    let dx = x2 - x1;
    let dy = y2 - y1;
    return sqrt(dx * dx + dy * dy);
}

let dist = distance(0, 0, 3, 4);
println("Distance from (0,0) to (3,4):", dist);

// Circle area and circumference
fn circle_area(radius) {
    return PI * radius * radius;
}

fn circle_circumference(radius) {
    return 2 * PI * radius;
}

let r = 5;
println("Circle with radius", to_str(r) + ":");
println("  Area:", circle_area(r));
println("  Circumference:", circle_circumference(r));

// Angle from point
fn angle_from_origin(x, y) {
    return atan2(y, x);
}

let angle = angle_from_origin(1, 1);
println("Angle from origin to (1,1):", angle, "radians");
println("  In degrees:", rad_to_deg(angle));

// Exponential growth
fn exponential_growth(initial, rate, time) {
    return initial * exp(rate * time);
}

let population = exponential_growth(100, 0.05, 10);
println("Population after 10 years (5% growth):", round(population));

// Projectile motion
fn projectile_height(v0, angle, t) {
    let g = 9.8;  // gravity
    let vy = v0 * sin(angle);
    return vy * t - 0.5 * g * t * t;
}

let h = projectile_height(20, deg_to_rad(45), 1);
println("Projectile height at t=1s:", h);

println();
println("Math demo complete!");
