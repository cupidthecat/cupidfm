// Test flow map with unquoted keys
print("Testing flow map with unquoted keys:");

// This should work
let d1 = yaml_parse("map: {a: 1, b: 2}\n");
print("Result:", typeof(d1));
if (d1 != nil) {
    print("Map type:", typeof(d1.map));
    if (typeof(d1.map) == "map") {
        print("Success! Keys:", keys(d1.map));
    }
}
