// Cover empty condition branch in C-style for parsing (cond == NULL)

let i = 0;
let ran = 0;

for (i = 0;; i = i + 1) {
  ran = ran + 1;
  break;
}

assert(ran == 1, "for empty condition executed once");
assert(i == 0, "break before incr");

print("for_c_style_empty_condition ok");
