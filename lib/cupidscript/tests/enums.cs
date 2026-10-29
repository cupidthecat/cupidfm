enum Color { Red, Green = 5, Blue }

assert(Color.Red == 0, "enum default start");
assert(Color.Green == 5, "enum explicit value");
assert(Color.Blue == 6, "enum auto increment");

print("enums ok");
