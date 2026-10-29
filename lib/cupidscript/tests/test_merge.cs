// Test merge operator in isolation
print("Test 1: Simple merge");
let yaml1 = "base: &base\n  x: 1\n  y: 2\nderived:\n  <<: *base\n  z: 3\n";
print("Parsing...");
let d1 = yaml_parse(yaml1);
print("Got result");
print("d1.derived.x = " + d1.derived.x);
print("d1.derived.y = " + d1.derived.y);
print("d1.derived.z = " + d1.derived.z);

if (d1.derived.x == 1 && d1.derived.y == 2 && d1.derived.z == 3) {
    print("PASS");
} else {
    print("FAIL");
}

print("\nTest complete");
