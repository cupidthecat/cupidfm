// Cover cs_parser.c rewind branches in C-style for parsing
// Init/incr begin with an identifier but are not assignments, so parser rewinds

let i = 0;
let sum = 0;

// init: `i + 0` is an expression (not assignment) -> triggers rewind path
for (i + 0; i < 3; i = i + 1) {
  sum = sum + i;
}

assert(sum == 3, "for init expr rewind");

let j = 0;

// incr: `j + 1` is an expression (not assignment) -> triggers rewind path
// Use cond=false to avoid an infinite loop (incr isn't executed)
for (j = 0; false; j + 1) {
  j = 999;
}

assert(j == 0, "for incr expr rewind");

print("for_c_style_rewind_expr_init_incr ok");
