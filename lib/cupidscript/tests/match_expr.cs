// match expression

let x = 2;
let label = match (x) {
  case 1: "one";
  case 2: "two";
  default: "other";
};
assert(label == "two", "match basic");

let y = 42;
let out = match (y) {
  case 1: 10;
  case 2: 20;
};
assert(out == nil, "match no default returns nil");

print("match_expr ok");
