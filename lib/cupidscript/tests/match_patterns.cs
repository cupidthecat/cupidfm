let xs = [1, 2];
let r1 = match (xs) {
  case [a, b]: a + b;
  default: 0;
};
assert(r1 == 3, "list pattern binds");

let m = {a: 10, b: 20};
let r2 = match (m) {
  case {a, b}: a + b;
  default: 0;
};
assert(r2 == 30, "map pattern binds");

let r3 = match (5) {
  case x if x > 3: "big";
  case _: "small";
};
assert(r3 == "big", "guarded pattern");

let r4 = match (nil) {
  case _: 1;
};
assert(r4 == 1, "wildcard matches");

print("match_patterns ok");
