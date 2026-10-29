// switch examples: ints/strings/bools/nil + default

fn describe(x) {
  switch (x) {
    case 0 { return "zero"; }
    case 1 { return "one"; }
    default { return "many/other"; }
  }
}

print("describe(0) =", describe(0));
print("describe(2) =", describe(2));

let color = "green";
switch (color) {
  case "red" { print("stop"); }
  case "green" { print("go"); }
  default { print("unknown"); }
}

let v = nil;
switch (v) {
  case nil { print("got nil"); }
  default { print("not nil"); }
}

let b = true;
switch (b) {
  case false { print("false"); }
  case true { print("true"); }
}

print("switch example ok");

