// Range operator + native range()

let xs = list();
for x in 1..=3 {
  push(xs, x);
}
assert(len(xs) == 3, "inclusive range length");
assert(xs[0] == 1, "range start");
assert(xs[2] == 3, "range end");

let ys = list();
for y in range(0, 4) { push(ys, y); } // 0,1,2,3
assert(len(ys) == 4, "range() length");
assert(ys[3] == 3, "range() last");

let zs = list();
for z in 0..3 {
  push(zs, z);
}
assert(len(zs) == 3, "exclusive range length");
assert(zs[2] == 2, "exclusive range end");

print("range ok");
