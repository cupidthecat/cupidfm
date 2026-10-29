// Pipe operator examples

fn add(a, b) { return a + b; }
fn double(x) => x * 2;

let a = 5 |> double();
let b = 5 |> add(3, _);
let c = 5 |> add(1) |> add(2);

print(a);
print(b);
print(c);
