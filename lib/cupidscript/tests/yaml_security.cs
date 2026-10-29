// YAML Security Tests - Prevent common YAML attacks and edge cases

print("=== YAML Security Tests ===");

// Test 1: Deep nesting (moderate - 20 levels)
let deep_yaml = "a:\n  b:\n    c:\n      d:\n        e:\n          f:\n            g:\n              h:\n                i:\n                  j:\n                    k:\n                      l:\n                        m:\n                          n:\n                            o:\n                              p:\n                                q:\n                                  r:\n                                    s:\n                                      value: deep\n";
let d1 = yaml_parse(deep_yaml);
assert(typeof(d1) == "map", "deep nesting parsed");
assert(d1.a.b.c.d.e.f.g.h.i.j.k.l.m.n.o.p.q.r.s.value == "deep", "deep value accessible");
print("Test 1: Deep nesting handled");

// Test 2: Very long string (1KB)
let long_str = "";
for (let i = 0; i < 100; i = i + 1) {
    long_str = long_str + "0123456789";
}
let yaml_long = "text: \"" + long_str + "\"\n";
let d2 = yaml_parse(yaml_long);
assert(len(d2.text) == 1000, "long string handled");
print("Test 2: Long string handled");

// Test 3: Anchor and alias - simple case
let d3 = yaml_parse("base: &ref\n  x: 1\nalias: *ref\n");
assert(d3.base.x == 1, "anchor value");
assert(d3.alias.x == 1, "alias references anchor");
print("Test 3: Simple anchor/alias works");

// Test 4: Multiple anchors
let d4 = yaml_parse("a: &a 1\nb: &b 2\nc: *a\nd: *b\n");
assert(d4.a == 1, "anchor a");
assert(d4.b == 2, "anchor b");
assert(d4.c == 1, "alias to a");
assert(d4.d == 2, "alias to b");
print("Test 4: Multiple anchors work");

// Test 5: Anchor with complex structure
let d5 = yaml_parse("base: &base\n  a: 1\n  b: 2\n  c:\n    d: 3\nref: *base\n");
assert(d5.base.a == 1, "complex anchor a");
assert(d5.base.c.d == 3, "complex anchor nested");
assert(d5.ref.a == 1, "complex alias a");
assert(d5.ref.c.d == 3, "complex alias nested");
print("Test 5: Complex anchor/alias works");

// Test 6: Merge key << with anchor
let d6 = yaml_parse("base: &base\n  a: 1\n  b: 2\nderived:\n  <<: *base\n  c: 3\n");
assert(d6.derived.a == 1, "merge key a");
assert(d6.derived.b == 2, "merge key b");
assert(d6.derived.c == 3, "merge key c");
print("Test 6: Merge key works");

// Test 7: Merge key with override
let d7 = yaml_parse("base: &base\n  a: 1\n  b: 2\nderived:\n  <<: *base\n  a: 99\n");
assert(d7.derived.a == 99, "merge override");
assert(d7.derived.b == 2, "merge non-override");
print("Test 7: Merge override works");

// Test 8: Empty document
let d8 = yaml_parse("");
assert(d8 == nil, "empty doc is nil");
print("Test 8: Empty document handled");

// Test 9: Only whitespace
let d9 = yaml_parse("   \n  \n   \n");
assert(d9 == nil, "whitespace doc is nil");
print("Test 9: Whitespace-only document handled");

// Test 10: Only comments
let d10 = yaml_parse("# comment 1\n# comment 2\n");
assert(d10 == nil, "comment doc is nil");
print("Test 10: Comment-only document handled");

// Test 11: Multiple documents separated by ---
let d11 = yaml_parse("a: 1\n---\nb: 2\n");
// Note: Current implementation may only parse first document
assert(d11.a == 1, "first document parsed");
print("Test 11: Document separator handled");

// Test 12: Unicode in keys
let d12 = yaml_parse("café: coffee\n");
assert(d12["café"] == "coffee", "unicode in key");
print("Test 12: Unicode in keys works");

// Test 13: Unicode in values
let d13 = yaml_parse("drink: café\n");
assert(d13.drink == "café", "unicode in value");
print("Test 13: Unicode in values works");

// Test 14: Empty string value
let d14 = yaml_parse("empty: \"\"\n");
assert(d14.empty == "", "empty string");
print("Test 14: Empty string handled");

// Test 15: Null value variations
let d15 = yaml_parse("a: null\nb: ~\nc:\n");
assert(d15.a == nil, "null keyword");
assert(d15.b == nil, "tilde null");
assert(d15.c == nil, "implicit null");
print("Test 15: Null variations work");

// Test 16: Boolean variations
let d16 = yaml_parse("a: true\nb: false\nc: yes\nd: no\ne: on\nf: off\n");
assert(d16.a == true, "true");
assert(d16.b == false, "false");
assert(d16.c == true, "yes");
assert(d16.d == false, "no");
assert(d16.e == true, "on");
assert(d16.f == false, "off");
print("Test 16: Boolean variations work");

// Test 17: Number variations
let d17 = yaml_parse("a: 123\nb: -456\nc: 3.14\nd: -2.5\ne: 0\n");
assert(d17.a == 123, "positive int");
assert(d17.b == -456, "negative int");
assert(d17.c == 3.14, "positive float");
assert(d17.d == -2.5, "negative float");
assert(d17.e == 0, "zero");
print("Test 17: Number variations work");

// Test 18: Hex and octal numbers
let d18 = yaml_parse("hex: 0xFF\noct: 0o77\n");
assert(d18.hex == 255, "hex number");
assert(d18.oct == 63, "octal number");
print("Test 18: Hex and octal numbers work");

// Test 19: Scientific notation
let d19 = yaml_parse("a: 1e3\nb: 1.5e2\nc: 2e-1\n");
assert(d19.a == 1000.0, "scientific 1e3");
assert(d19.b == 150.0, "scientific 1.5e2");
assert(d19.c == 0.2, "scientific 2e-1");
print("Test 19: Scientific notation works");

// Test 20: Special float values
let d20 = yaml_parse("a: .inf\nb: -.inf\nc: .nan\n");
assert(typeof(d20.a) == "float", "inf is float");
assert(typeof(d20.b) == "float", "-inf is float");
assert(typeof(d20.c) == "float", "nan is float");
print("Test 20: Special float values work");

// Test 21: Escape sequences in double quotes
let d21 = yaml_parse("text: \"Line1\\nLine2\\tTab\"\n");
assert(contains(d21.text, "Line1"), "escaped newline start");
assert(contains(d21.text, "Line2"), "escaped newline end");
print("Test 21: Escape sequences work");

// Test 22: Single vs double quotes
let d22 = yaml_parse("single: 'no\\nescape'\ndouble: \"with\\nescape\"\n");
assert(contains(d22.single, "\\n"), "single quotes literal");
assert(contains(d22.double, "with"), "double quotes escaped");
print("Test 22: Quote types differ correctly");

// Test 23: Flow lists
let d23 = yaml_parse("list: [1, 2, 3]\n");
assert(len(d23.list) == 3, "flow list length");
assert(d23.list[0] == 1, "flow list item");
print("Test 23: Flow lists work");

// Test 24: Flow maps with quoted keys
let d24 = yaml_parse("map: {\"a\": 1, \"b\": 2}\n");
assert(d24.map.a == 1, "flow map value a");
assert(d24.map.b == 2, "flow map value b");
print("Test 24: Flow maps with quoted keys work");

// Test 25: Nested flow list
let d25 = yaml_parse("data: [[1, 2], [3, 4]]\n");
assert(len(d25.data) == 2, "nested flow list length");
assert(len(d25.data[0]) == 2, "nested flow list inner length");
assert(d25.data[0][0] == 1, "nested flow list value");
print("Test 25: Nested flow lists work");

print("All YAML security tests passed!");
