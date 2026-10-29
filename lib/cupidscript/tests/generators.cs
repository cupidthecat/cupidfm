fn range(n) {
  let i = 0;
  while (i < n) {
    yield i;
    i += 1;
  }
}

let xs = range(4);
assert(len(xs) == 4, "generator length");
assert(xs[0] == 0, "generator first");
assert(xs[3] == 3, "generator last");

print("generators ok");
