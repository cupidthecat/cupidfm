// Numeric literals + comments

// decimal with underscores
assert(1_000_000 == 1000000, "underscored int");

// hex literals
assert(0xFF == 255, "hex int");
assert(0X10 == 16, "hex uppercase X");

// floats
let f = 3.14;
assert(typeof(f) == "float", "float literal type");
assert(f > 3.1 && f < 3.2, "float literal value");

// line comments
let a = 1; // comment after code
assert(a == 1, "line comment");

/* block comments */
let b = 2;
/* multi
   line
   block */
assert(b == 2, "block comment");

print("literals ok");
