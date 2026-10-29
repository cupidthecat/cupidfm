// Demonstrates: modules (require exports), literals, loops, indexing, fmt, string/path helpers

let lib = require("lib.cs");
print(lib.hello("world"));

let xs = [10, 20];
push(xs, 30);
xs[1] = 99;
print("xs len =", len(xs), "xs =", xs[0], xs[1], xs[2]);

let m = lib.make_sample_data();
print("name =", m["name"], "age =", m["age"]);
print("tags len =", len(m["tags"]), "tag0 =", m["tags"][0]);

print("keys/values/items:");
for k in keys(m) { print(" ", k, "=", m[k]); }

print(fmt("fmt: x=%d s=%s b=%b v=%v", 42, "ok", true, m));

let parts = str_split("a,b,c", ",");
print("split len =", len(parts), "parts[2] =", parts[2]);
print("join =", join(parts, "|"));

print("basename =", path_basename("/tmp/a/b.txt"), "ext =", path_ext("/tmp/a/b.txt"));

// break/continue
let i = 0;
let sum = 0;
while (true) {
  i += 1;
  if (i > 10) { break; }
  if (i % 2 == 0) { continue; }
  sum += i;
}
print("sum of odds 1..10 =", sum);
