let xs = [10, 20, 30];

let pairs = enumerate(xs);
assert(len(pairs) == 3, "enumerate len");
assert(pairs[0][0] == 0 && pairs[0][1] == 10, "enumerate first");
assert(pairs[2][0] == 2 && pairs[2][1] == 30, "enumerate last");

let zs = zip([1, 2, 3], ["a", "b"]);
assert(len(zs) == 2, "zip len");
assert(zs[0][0] == 1 && zs[0][1] == "a", "zip first");
assert(zs[1][0] == 2 && zs[1][1] == "b", "zip second");

assert(any([0, nil, 3]) == true, "any truthy");
assert(any([0, nil]) == true, "any int truthy");
assert(any([nil, false]) == false, "any false");

fn is_even(x) { return x % 2 == 0; }
assert(any(xs, is_even) == true, "any predicate");
assert(all([2, 4, 6], is_even) == true, "all predicate");
assert(all([2, 3, 4], is_even) == false, "all predicate false");

let evens = filter([11, 20, 31], is_even);
assert(len(evens) == 1 && evens[0] == 20, "filter");

fn dbl(x) { return x * 2; }
let ys = map(xs, dbl);
assert(len(ys) == 3 && ys[1] == 40, "map");

fn sum(a, b) { return a + b; }
assert(reduce([1, 2, 3, 4], sum) == 10, "reduce no init");
assert(reduce([1, 2, 3], sum, 10) == 16, "reduce init");

print("iteration_helpers ok");
