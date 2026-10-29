// Closures + function literals

fn make_counter() {
  let n = 0;
  return fn() { n = n + 1; return n; };
}

let c = make_counter();
assert(c() == 1, "closure 1");
assert(c() == 2, "closure 2");
assert(c() == 3, "closure 3");

// Closures should capture independently
let a = make_counter();
let b = make_counter();
assert(a() == 1, "counter a 1");
assert(b() == 1, "counter b 1");
assert(a() == 2, "counter a 2");

print("closures ok");
