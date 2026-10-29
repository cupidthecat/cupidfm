fn fib(n) {
  let a = 0;
  let b = 1;
  let i = 0;
  while (i < n) {
    yield a;
    let next = a + b;
    a = b;
    b = next;
    i += 1;
  }
}

let xs = fib(8);
print(xs);
