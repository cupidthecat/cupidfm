// switch pattern matching demo

let value = {x: 2, y: 3, z: 4};

switch (value) {
  case {x, y} {
    print("x+y =", x + y);
  }
  case _ {
    print("no match");
  }
}

let v2 = [10, 20, 30];
switch (v2) {
  case [a, b, ...rest] {
    print("a=", a, "b=", b, "rest=", rest);
  }
}

let n = 7;
switch (n) {
  case int(x) { print("int:", x); }
  default { print("not int"); }
}
