// Compound assignment operators

let x = 1;
x += 2;
assert(x == 3, "+=");

x -= 1;
assert(x == 2, "-=");

x *= 5;
assert(x == 10, "*=");

x /= 2;
assert(typeof(x) == "float", "/= returns float");
assert(x > 4.9 && x < 5.1, "/= value");

print("compound_assign ok");
