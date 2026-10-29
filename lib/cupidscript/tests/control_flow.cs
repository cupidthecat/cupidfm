// Control-flow + loop semantics

let sum = 0;
let i = 0;
while (i < 10) {
  i = i + 1;
  if (i % 2 == 0) { continue; }
  if (i == 9) { break; }
  sum = sum + i;
}

// i hits: 1,3,5,7 then break at 9 (and evens skipped)
assert(sum == 16, "while/continue/break broken");

// for-in over list
let xs = [1, 2, 3, 4];
let s2 = 0;
for x in xs {
  s2 = s2 + x;
}
assert(s2 == 10, "for-in list broken");

// C-style for loop
let s3 = 0;
let j = 0;
for (j = 0; j < 5; j = j + 1) {
  s3 = s3 + j;
}
assert(s3 == 10, "C-style for broken");

print("control_flow ok");
