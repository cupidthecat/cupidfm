// Compound assignment on fields

let m = {"a": 1};
m.a += 1;
assert(m.a == 2, "field +=");

m.a *= 5;
assert(m.a == 10, "field *=");
