// Pipe operator

fn add(a, b) { return a + b; }
fn double(x) => x * 2;

let r1 = 5 |> double();
assert(r1 == 10, "pipe to call");

let r2 = 5 |> add(3, _);
assert(r2 == 8, "pipe placeholder");

let r3 = 5 |> add(_, 3);
assert(r3 == 8, "pipe placeholder first");

let r4 = 5 |> add(1) |> add(2);
assert(r4 == 8, "pipe chain");

print("pipe_operator ok");
