// Test 1: Flat map
let flat = yaml_parse("a: 1\nb: 2");
print("Test 1 - Flat: a=" + flat.a + ", b=" + flat.b);

// Test 2: Nested map indented
let nested = yaml_parse("outer:\n  inner: 42");
print("Test 2 - Nested indented: typeof(outer)=" + typeof(nested.outer));
if (typeof(nested.outer) == "map") {
    print("Test 2 - inner=" + nested.outer.inner);
}
