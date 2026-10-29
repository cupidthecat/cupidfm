// Cover else-if vs else-block parsing branches

let x = 0;
if (false) {
  x = 1;
} else if (true) {
  x = 2;
} else {
  x = 3;
}
assert(x == 2, "else-if path");

let y = 0;
if (false) {
  y = 1;
} else {
  y = 4;
}
assert(y == 4, "else block path");

print("if_else_variants ok");
