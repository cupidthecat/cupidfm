let xs = [1, 2, 3, 4, 5];

print("enumerate:", enumerate(xs));
print("zip:", zip(["a", "b", "c"], [10, 20, 30]));

fn is_even(x) { return x % 2 == 0; }
print("any even?", any(xs, is_even));
print("all even?", all(xs, is_even));

print("filter evens:", filter(xs, is_even));
print("map square:", map(xs, fn(x) => x * x));

fn sum(a, b) { return a + b; }
print("reduce sum:", reduce(xs, sum));
print("reduce sum with init:", reduce(xs, sum, 100));
