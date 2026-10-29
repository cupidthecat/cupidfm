// Test flow map with and without spaces
print("Test 1: Flow map with space after colon");
let d1 = yaml_parse("map: {a: 1, b: 2}\n");
print("Result:", typeof(d1));

print("\nTest 2: Flow map without space after colon");
let d2 = yaml_parse("map: {a:1, b:2}\n");
print("Result:", typeof(d2));
