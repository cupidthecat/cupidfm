fn inner() {
  // runtime error
  let x = 1 / 0;
  return x;
}

fn outer() {
  return inner();
}

outer();

