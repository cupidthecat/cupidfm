// Exercise the '..' and '..=' range operators in the VM.

fn collect(r) { let xs = list(); for x in r { push(xs, x); } return xs; }

let a = collect(5..1);      // descending exclusive: 5,4,3,2
assert(len(a) == 4, "range desc exclusive len");
assert(a[0] == 5 && a[3] == 2, "range desc exclusive values");

let b = collect(5..=1);     // descending inclusive: 5,4,3,2,1
assert(len(b) == 5, "range desc inclusive len");
assert(b[0] == 5 && b[4] == 1, "range desc inclusive values");

let c = collect(1..1);      // empty
assert(len(c) == 0, "range equal exclusive empty");

let d = collect(1..=1);     // single
assert(len(d) == 1 && d[0] == 1, "range equal inclusive single");

let e = collect(1.9..3.1);  // floats are truncated to ints: 1..3 => [1,2]
assert(len(e) == 2 && e[0] == 1 && e[1] == 2, "range float endpoints");

print("vm_range_operator_edges ok");

