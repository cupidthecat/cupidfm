// Simple tests for operator precedence and grouping using our assert native
assert(1 + 2 * 3 == 7, "precedence broken");
assert((1 + 2) * 3 == 9, "paren grouping broken");
print("math ok");
