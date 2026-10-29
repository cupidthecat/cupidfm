// range() with a negative step

let xs = list();
for x in range(5, 0, -2) { push(xs, x); }
assert(len(xs) == 3, "range negative step len");
assert(xs[0] == 5, "range negative step 0");
assert(xs[1] == 3, "range negative step 1");
assert(xs[2] == 1, "range negative step 2");

print("range_negative_step ok");
