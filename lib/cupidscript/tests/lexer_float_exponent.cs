// Float exponent + underscore branches in lexer

// exponent without decimal
let a = 1e3;
assert(abs(a - 1000) < 0.000001, "1e3");

// exponent with decimal + negative exponent
let b = 1.5e-3;
assert(abs(b - 0.0015) < 0.000000001, "1.5e-3");

// uppercase E
let c = 2E2;
assert(abs(c - 200) < 0.000001, "2E2");

// underscores inside fractional digits
let d = 12.3_4;
assert(abs(d - 12.34) < 0.000001, "12.3_4");

print("lexer_float_exponent ok");
