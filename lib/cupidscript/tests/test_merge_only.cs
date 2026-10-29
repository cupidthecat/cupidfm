// Test ONLY merge operator
print("Testing merge operator only");

print("Test 18: Merge with single map");
let d18 = yaml_parse("base: &base\n  x: 1\n  y: 2\nderived:\n  <<: *base\n  z: 3\n");
print("Checking d18.derived.x...");
assert(d18.derived.x == 1, "merge x");
print("Checking d18.derived.y...");
assert(d18.derived.y == 2, "merge y");
print("Checking d18.derived.z...");
assert(d18.derived.z == 3, "merge z");
print("Test 18 passed");

print("\nTest 19: Merge with list of maps");
let d19 = yaml_parse("a: &a\n  x: 1\nb: &b\n  y: 2\nc:\n  <<: [*a, *b]\n  z: 3\n");
print("Checking d19.c.x...");
assert(d19.c.x == 1, "merge list x");
print("Checking d19.c.y...");
assert(d19.c.y == 2, "merge list y");
print("Checking d19.c.z...");
assert(d19.c.z == 3, "merge list z");
print("Test 19 passed");

print("\nAll merge tests passed!");
