let v = [1, 2, 3];
let sum = 0;

switch (v) {
  case [a, b, ...rest] { sum = a + b + rest[0]; }
}

assert(sum == 6, "list pattern destructuring");

let m = {x: 5, y: 7};
let out = 0;

switch (m) {
  case {x, y: yy} { out = x + yy; }
}

assert(out == 12, "map pattern destructuring");

let t = 123;
let hit = "";

switch (t) {
  case int(x) { hit = "int:" + x; break; }
  default { hit = "no"; }
}

assert(hit == "int:123", "type pattern int");

let p = promise();
resolve(p, "ok");
let res = "";

switch (p) {
  case promise(x) { res = await x; }
}

assert(res == "ok", "type pattern promise");

print("switch_patterns ok");
