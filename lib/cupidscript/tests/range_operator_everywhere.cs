// Range operator usable in expressions and for-in

let xs = list();
for x in 0..3 { push(xs, x); }
assert(len(xs) == 3, "range list len");
assert(xs[0] == 0 && xs[2] == 2, "range list contents");

let ys = list();
for y in 1..=3 { push(ys, y); }
assert(len(ys) == 3, "inclusive range len");
assert(ys[2] == 3, "inclusive range last");

let sum = 0;
for i in 0..=4 {
  sum = sum + i;
}
assert(sum == 10, "for-in range sum");

print("range_operator_everywhere ok");
