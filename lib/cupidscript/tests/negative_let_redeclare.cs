// Redeclaring a let in same scope overwrites the binding

let x = 1;
assert(x == 1, "initial let");

let x = 2;
assert(x == 2, "redeclare overwrites");

print("let_redeclare ok");
