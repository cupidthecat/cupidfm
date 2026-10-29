// break inside a switch exits the switch (not the surrounding loop)

let sum = 0;
for i in 0..5 {
  switch (i) {
    case 2 { break; }          // exits only the switch
    default { sum += i; }
  }
  sum += 10;                   // still runs for i == 2
}

print("sum =", sum);
print("switch_break_in_loop example ok");

