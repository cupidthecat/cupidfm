// Test all new language features

print("=== FLOATS ===");
let pi = 3.14159;
let e = 2.71828;
print("pi =", pi);
print("e =", e);
print("pi * 2 =", pi * 2);
print("sqrt(2) =", sqrt(2));
print("pow(2, 8) =", pow(2, 8));

print("\n=== RANGE FUNCTION ===");
print("range(5):");
for i in range(5) {
  print("  ", i);
}

print("range(2, 7):");
for i in range(2, 7) {
  print("  ", i);
}

print("range(0, 10, 2):");
for i in range(0, 10, 2) {
  print("  ", i);
}

print("range(10, 0, -2):");
for i in range(10, 0, -2) {
  print("  ", i);
}

print("\n=== TYPE PREDICATES ===");
print("is_int(42):", is_int(42));
print("is_float(3.14):", is_float(3.14));
print("is_string(\"hi\"):", is_string("hi"));
print("is_list([1,2]):", is_list([1, 2]));
print("is_map({}):", is_map({}));
print("is_nil(nil):", is_nil(nil));
print("is_bool(true):", is_bool(true));
print("is_function(print):", is_function(print));

print("\n=== MATH FUNCTIONS ===");
print("abs(-5):", abs(-5));
print("abs(-3.7):", abs(-3.7));
print("min(3, 7, 2, 9):", min(3, 7, 2, 9));
print("max(3, 7, 2, 9):", max(3, 7, 2, 9));
print("floor(3.7):", floor(3.7));
print("ceil(3.2):", ceil(3.2));
print("round(3.5):", round(3.5));
print("round(3.4):", round(3.4));
print("sqrt(16):", sqrt(16));
print("pow(2, 10):", pow(2, 10));

print("\n=== PRACTICAL EXAMPLES ===");

// Calculate circle area
fn circle_area(radius) {
  return pi * radius * radius;
}
print("Area of circle r=5:", circle_area(5));

// Sum a range
fn sum_range(start, end) {
  let total = 0;
  for i in range(start, end) {
    total = total + i;
  }
  return total;
}
print("Sum 1..10:", sum_range(1, 11));

// Filter using type predicates
let mixed = [1, "hi", 3.14, nil, true, 42];
let numbers = [];
for item in mixed {
  if (is_int(item) || is_float(item)) {
    push(numbers, item);
  }
}
print("Numbers from mixed list:", numbers);

// Quadratic formula
fn quadratic(a, b, c) {
  let discriminant = b * b - 4.0 * a * c;
  if (discriminant < 0) {
    return nil;
  }
  let sqrt_disc = sqrt(discriminant);
  let x1 = (-b + sqrt_disc) / (2.0 * a);
  let x2 = (-b - sqrt_disc) / (2.0 * a);
  return [x1, x2];
}

let solutions = quadratic(1.0, -3.0, 2.0);
print("Solutions to x^2 - 3x + 2 = 0:", solutions);

print("\n=== ELSE-IF CHAINS ===");
fn classify_number(n) {
  if (n < 0) {
    return "negative";
  } else if (n == 0) {
    return "zero";
  } else if (n < 10) {
    return "small positive";
  } else if (n < 100) {
    return "medium positive";
  } else {
    return "large positive";
  }
}

for n in [-5, 0, 3, 50, 200] {
  print("classify_number(" + to_str(n) + "):", classify_number(n));
}

print("\nAll tests completed!");
