// Operators, precedence, numeric conversions

// arithmetic + precedence
assert(1 + 2 * 3 == 7, "precedence");
assert((1 + 2) * 3 == 9, "parens");

// / always returns float
let d = 5 / 2;
assert(typeof(d) == "float", "division returns float");
assert(d > 2.4 && d < 2.6, "division value");

// mixed int/float arithmetic
let m = 1 + 2.5;
assert(typeof(m) == "float", "mixed arithmetic");

// string concatenation
assert("a" + "b" == "ab", "string concat");
assert("x" + 1 == "x1", "string + int");
assert(1 + "x" == "1x", "int + string");

// comparisons
assert(2 < 3, "lt");
assert(2 <= 2, "le");
assert(3 > 2, "gt");
assert(3 >= 3, "ge");

// equality: int==float allowed
assert(2 == 2.0, "int == float");
assert(2.0 == 2, "float == int");
assert(2 != 3.0, "neq");

print("operators ok");
