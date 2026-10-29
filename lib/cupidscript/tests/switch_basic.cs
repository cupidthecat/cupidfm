// switch basic matching + default + fallthrough

let hit = "";

switch (2) {
  case 1 { hit = "one"; break; }
  case 2 { hit = "two"; break; }
  default { hit = "default"; }
}
assert(hit == "two", "int match");

hit = "";
switch ("b") {
  case "a" { hit = "a"; break; }
  case "b" { hit = "b"; break; }
  default { hit = "d"; }
}
assert(hit == "b", "string match");

hit = "";
switch (true) {
  case false { hit = "f"; break; }
  case true { hit = "t"; }
}
assert(hit == "t", "bool match");

hit = "";
switch (nil) {
  case 0 { hit = "zero"; break; }
  case nil { hit = "nil"; break; }
  default { hit = "d"; }
}
assert(hit == "nil", "nil match");

hit = "";
switch (123) {
  case 1 { hit = "no"; break; }
  default { hit = "yes"; }
}
assert(hit == "yes", "default hit");

// Ensure fallthrough works when break is omitted.
let c = 0;
switch (1) {
  case 1 { c += 1; }
  case 1 { c += 100; }
  default { c += 1000; }
}
assert(c == 1101, "fallthrough");

print("switch_basic ok");

