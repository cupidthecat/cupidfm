// break inside a switch should exit the switch, not the loop that contains it.

let sum = 0;
for i in 0..4 {
  switch (i) {
    case 2 { break; }
    default { sum += i; }
  }
  // Always executed (even when break exits switch).
  sum += 10;
}

// i=0: default +0, +10
// i=1: default +1, +10
// i=2: break,      +10
// i=3: default +3, +10
assert(sum == (0+10) + (1+10) + (10) + (3+10), "break exits switch only");

print("switch_break_inside_loop ok");

