// Range inclusive/exclusive edge cases

fn collect(r) { let xs = list(); for x in r { push(xs, x); } return xs; }

let r0 = collect(0..0);
assert(len(r0) == 0, "exclusive single-element is empty");

let r0i = collect(0..=0);
assert(len(r0i) == 1, "inclusive single-element len");
assert(r0i[0] == 0, "inclusive single-element contains value");

let r1 = collect(0..3);
assert(len(r1) == 3, "exclusive ascending len");
assert(r1[0] == 0 && r1[1] == 1 && r1[2] == 2, "exclusive ascending elems");

let r1i = collect(0..=3);
assert(len(r1i) == 4, "inclusive ascending len");
assert(r1i[0] == 0 && r1i[1] == 1 && r1i[2] == 2 && r1i[3] == 3, "inclusive ascending elems");

let r2 = collect(3..0);
assert(len(r2) == 3, "exclusive descending len");
assert(r2[0] == 3 && r2[1] == 2 && r2[2] == 1, "exclusive descending elems");

let r2i = collect(3..=0);
assert(len(r2i) == 4, "inclusive descending len");
assert(r2i[0] == 3 && r2i[1] == 2 && r2i[2] == 1 && r2i[3] == 0, "inclusive descending elems");

print("range_inclusive ok");
