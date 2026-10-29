// switch fallthrough demo

let x = 2;
let log = [];

switch (x) {
  case 1 { push(log, "one"); }
  case 2 { push(log, "two"); }
  case 3 { push(log, "three"); break; }
  default { push(log, "other"); }
}

print("log:", log); // expected: ["two", "three"]

let y = 99;
let log2 = [];

switch (y) {
  case 1 { push(log2, "one"); }
  default { push(log2, "default"); }
  case 2 { push(log2, "two"); }
}

print("log2:", log2); // expected: ["default", "two"]
