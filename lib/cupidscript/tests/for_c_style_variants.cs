// C-style for variants (optional init/incr)

let sum = 0;
let i = 0;
for (; i < 5; ) {
  sum = sum + i;
  i = i + 1;
}
assert(sum == 10, "for(;cond;) works");

let sum2 = 0;
for (i = 0; i < 5; i = i + 1) {
  sum2 = sum2 + i;
}
assert(sum2 == 10, "normal c-style for works");

print("for_c_style_variants ok");
