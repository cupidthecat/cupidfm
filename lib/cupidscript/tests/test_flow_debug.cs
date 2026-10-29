// Test to debug flow map parsing
print("Test 1: Simple block map");
let d1 = yaml_parse("a: 1\n");
print("Keys:", keys(d1));
print("Value:", d1.a);

print("\nTest 2: Block map with multiple items");
let d2 = yaml_parse("a: 1\nb: 2\n");
print("Keys:", keys(d2));

print("\nTest 3: Flow list with plain values");
let d3 = yaml_parse("list: [a, b]\n");
print("List:", d3.list);

print("\nTest 4: Flow map with ONE item, quoted");
let d4 = yaml_parse("map: {\"a\": 1}\n");
print("Result:", typeof(d4.map));

print("\nTest 5: Flow map with ONE item, unquoted");
let d5 = yaml_parse("map: {a: 1}\n");
print("Result:", typeof(d5));
