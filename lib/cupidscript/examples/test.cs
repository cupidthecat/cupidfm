// Cupidscript v0 interpreter test

print("=== Cupidscript v0 test ===");

// basics
let a = 7;
let b = 3;
print("a=", a, "b=", b);
print("a+b=", a + b);
print("a-b=", a - b);
print("a*b=", a * b);
print("a/b=", a / b);
print("a%b=", a % b);

// strings + concatenation
let s = "hello";
let t = "world";
print("concat:", s + " " + t + "!");
print("mix:", "a=" + a + ", b=" + b); // int->string via +

print("typeof(a) =", typeof(a));
print("typeof(s) =", typeof(s));
print("typeof(nil) =", typeof(nil));

// if/else + comparisons
if (a > b) {
  print("a is greater than b");
} else {
  print("a is not greater than b");
}

if ((a == 7) && (b != 9)) {
  print("logic ok");
}

// while loop
let i = 0;
let sum = 0;
while (i < 10) {
  sum = sum + i;
  i = i + 1;
}
print("sum 0..9 =", sum);

// functions
fn add(x, y) {
  return x + y;
}
print("add(20,22) =", add(20, 22));

// recursion: factorial
fn fact(n) {
  if (n <= 1) { return 1; }
  return n * fact(n - 1);
}
print("fact(6) =", fact(6));

// fibonacci (iterative)
fn fib(n) {
  if (n <= 0) { return 0; }
  if (n == 1) { return 1; }
  let x = 0;
  let y = 1;
  let k = 2;
  while (k <= n) {
    let z = x + y;
    x = y;
    y = z;
    k = k + 1;
  }
  return y;
}
print("fib(10) =", fib(10));

// getenv test (may print nil depending on your env)
let home = getenv("HOME");
print("HOME =", home);
if (home != nil) {
  print("HOME length-ish test:", "HOME=" + home);
}

print("=== done ===");
