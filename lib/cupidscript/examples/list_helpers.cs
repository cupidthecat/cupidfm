// List helper example

let a = [1, 2];
let b = [3, 4];
extend(a, b);
print(a); // [1, 2, 3, 4]

print(index_of(a, 3)); // 2
print(contains(a, 4)); // true

sort(a);
print(a); // [1, 2, 3, 4]

sort(a, "quick");
print(a); // [1, 2, 3, 4]

sort(a, "merge");
print(a); // [1, 2, 3, 4]

fn desc(x, y) { return y - x; }
sort(a, desc, "quick");
print(a); // [4, 3, 2, 1]
