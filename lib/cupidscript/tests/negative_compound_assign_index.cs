// Compound assignment on index expressions

let xs = [1];
xs[0] += 1;
assert(xs[0] == 2, "index +=");

xs[0] -= 1;
assert(xs[0] == 1, "index -=");
