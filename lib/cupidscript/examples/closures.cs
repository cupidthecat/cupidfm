// Demonstrates function references and closures (captured variables)

fn make_counter() {
  let n = 0;
  return fn() {
    n = n + 1;
    return n;
  };
}

let c = make_counter();
print("counter:", c(), c(), c());

fn greet(name) { return "hello " + name; }
let fnref = greet;
print(fnref("world"));

