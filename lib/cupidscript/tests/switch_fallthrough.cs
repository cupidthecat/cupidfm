let x = 1;
let out = [];

switch (x) {
  case 1 { push(out, "one"); }
  case 2 { push(out, "two"); break; }
  default { push(out, "other"); }
}

assert(len(out) == 2, "fallthrough count");
assert(out[0] == "one", "fallthrough first");
assert(out[1] == "two", "fallthrough second");

let y = 9;
let out2 = [];

switch (y) {
  case 1 { push(out2, "one"); }
  default { push(out2, "default"); }
  case 3 { push(out2, "three"); }
}

assert(len(out2) == 2, "default fallthrough count");
assert(out2[0] == "default", "default runs when no match");
assert(out2[1] == "three", "default falls through");

let z = 3;
let out3 = [];

switch (z) {
  case 1 { push(out3, "one"); }
  default { push(out3, "default"); }
  case 3 { push(out3, "three"); }
}

assert(len(out3) == 1, "match after default count");
assert(out3[0] == "three", "match after default skips default");

print("switch_fallthrough ok");
