// Anonymous function literals

let add = fn(a, b) {
  return a + b;
};

assert(add(4, 5) == 9, "anon function call");

// higher-order
fn apply(f, x) {
  return f(x);
}

let inc = fn(x) { return x + 1; };
assert(apply(inc, 9) == 10, "higher-order");

print("anon_functions ok");
