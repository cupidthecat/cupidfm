// Demonstrates: uncaught throw produces a stack trace

fn inner() {
  throw "boom";
}

fn outer() {
  inner();
}

outer();

