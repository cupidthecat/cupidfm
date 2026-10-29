// Test plain scalar parsing
print("Test 1: Plain scalar alone");
let d1 = yaml_parse("value: a\n");
print("Result:", d1.value);
print("Type:", typeof(d1.value));

print("\nTest 2: Plain scalar with colon after");
let d2 = yaml_parse("a: 1\n");
print("Result keys:", keys(d2));
print("Value:", d2.a);

print("\nTest 3: Quoted keys in flow map");
let d3 = yaml_parse("map: {\"a\": 1, \"b\": 2}\n");
print("Result:", typeof(d3.map));
if (typeof(d3.map) == "map") {
    print("Keys:", keys(d3.map));
}

print("\nTest 4: List with plain values in flow");
let d4 = yaml_parse("list: [a, b, c]\n");
print("Result:", typeof(d4.list));
if (typeof(d4.list) == "list") {
    print("Length:", len(d4.list));
    print("First:", d4.list[0]);
}
