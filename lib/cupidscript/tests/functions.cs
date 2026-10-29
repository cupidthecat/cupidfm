// Functions: definition, return, recursion

fn add(a, b) { return a + b; }
assert(add(2, 3) == 5, "call/return");

fn early(x) {
  if (x > 0) { return 1; }
  return 2;
}
assert(early(1) == 1, "early return then");
assert(early(0) == 2, "early return else");

fn fact(n) {
  if (n <= 1) { return 1; }
  return n * fact(n - 1);
}
assert(fact(5) == 120, "recursion");

print("functions ok");
