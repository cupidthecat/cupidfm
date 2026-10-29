// defer statement

let xs = [];

fn f() {
  defer { push(xs, "a"); }
  defer { push(xs, "b"); }
  return 1;
}

let v = f();
assert(v == 1, "return value");
assert(xs[0] == "b" && xs[1] == "a", "defer LIFO order");

print("defer ok");
