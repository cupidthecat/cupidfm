struct Point { x, y = 0 }

let p = Point(3);
assert(p.x == 3, "struct field x");
assert(p.y == 0, "struct default y");

let q = Point(1, 2);
assert(q.x == 1, "struct positional x");
assert(q.y == 2, "struct positional y");

p.x = 5;
assert(p.x == 5, "struct field set");

print("structs ok");
