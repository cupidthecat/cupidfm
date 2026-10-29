// && and || should short-circuit (do not evaluate RHS when not needed)

let calls = 0;
fn bump() {
  calls = calls + 1;
  return true;
}

// false && bump() => bump not called
let a = false && bump();
assert(calls == 0, "&& short-circuit");
assert(a == false, "&& result");

// true || bump() => bump not called
let b = true || bump();
assert(calls == 0, "|| short-circuit");
assert(b == true, "|| result");

// true && bump() => bump called
let c = true && bump();
assert(calls == 1, "&& evaluates rhs when needed");
assert(c == true, "&& rhs result");

// false || bump() => bump called
let d = false || bump();
assert(calls == 2, "|| evaluates rhs when needed");
assert(d == true, "|| rhs result");

print("short_circuit ok");
