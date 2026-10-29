// Advanced YAML parsing tests - YAML 1.2.2 features

print("=== Advanced YAML Tests ===");

// Test 1: Chomping indicators - strip (|-)
let d1 = yaml_parse("text: |-\n  line1\n  line2\n");
assert(d1.text == "line1\nline2", "chomping strip");

// Test 2: Chomping indicators - keep (|+)
let d2 = yaml_parse("text: |+\n  line1\n  line2\n\n");
assert(d2.text == "line1\nline2\n\n", "chomping keep");

// Test 3: Chomping indicators - clip (|)
let d3 = yaml_parse("text: |\n  line1\n  line2\n");
assert(d3.text == "line1\nline2\n", "chomping clip (default)");

// Test 4: Explicit indentation indicator (|2)
let d4 = yaml_parse("text: |2\n  line1\n  line2\n");
assert(contains(d4.text, "line1") && contains(d4.text, "line2"), "explicit indent");

// Test 5: Combined indicators (|2-)
let d5 = yaml_parse("text: |2-\n  line1\n  line2\n");
assert(contains(d5.text, "line1"), "combined indicators");

// Test 6: Folded scalar with chomp (>-)
let d6 = yaml_parse("text: >-\n  folded\n  text\n");
assert(contains(d6.text, "folded"), "folded with strip");

// Test 7: Tag !!str
let d7 = yaml_parse("num: !!str 123\n");
assert(d7.num == "123", "!!str tag");

// Test 8: Tag !!int
let d8 = yaml_parse("num: !!int \"456\"\n");
assert(d8.num == 456, "!!int tag");

// Test 9: Tag !!float
let d9 = yaml_parse("num: !!float \"3.14\"\n");
assert(d9.num == 3.14, "!!float tag");

// Test 10: Tag !!bool
let d10 = yaml_parse("flag: !!bool yes\n");
assert(d10.flag == true, "!!bool tag true");

// Test 11: Tag !!bool false
let d11 = yaml_parse("flag: !!bool no\n");
assert(d11.flag == false, "!!bool tag false");

// Test 12: Tag !!null
let d12 = yaml_parse("value: !!null something\n");
assert(d12.value == nil, "!!null tag");

// Test 13: %YAML directive
let d13 = yaml_parse("%YAML 1.2\n---\nkey: value\n");
assert(d13.key == "value", "%YAML directive");

// Test 14: Multiple documents with ---
let docs14 = yaml_parse_all("---\na: 1\n---\nb: 2\n");
assert(len(docs14) == 2, "multi-doc count");
assert(docs14[0].a == 1, "multi-doc first");
assert(docs14[1].b == 2, "multi-doc second");

// Test 15: Document end marker ...
let docs15 = yaml_parse_all("a: 1\n...\n---\nb: 2\n");
assert(len(docs15) == 2, "doc end marker count");
assert(docs15[0].a == 1, "doc with ... first");
assert(docs15[1].b == 2, "doc with ... second");

// Test 16: Explicit key indicator ?
let d16 = yaml_parse("? key1\n: value1\n");
assert(d16.key1 == "value1", "explicit key ?");

// Test 17: Complex key (list as key - converted to string)
let d17 = yaml_parse("? [a, b]\n: complex\n");
assert(typeof(d17) == "map", "complex key parsed");

// Test 18: Merge operator << with single map
let d18 = yaml_parse("base: &base\n  x: 1\n  y: 2\nderived:\n  <<: *base\n  z: 3\n");
assert(d18.derived.x == 1, "merge operator x");
assert(d18.derived.y == 2, "merge operator y");
assert(d18.derived.z == 3, "merge operator z");

// Test 19: Merge operator with list of maps
let d19 = yaml_parse("a: &a\n  x: 1\nb: &b\n  y: 2\nc:\n  <<: [*a, *b]\n  z: 3\n");
assert(d19.c.x == 1, "merge list x");
assert(d19.c.y == 2, "merge list y");
assert(d19.c.z == 3, "merge list z");

// Test 20: Unicode escape \uXXXX
let d20 = yaml_parse("text: \"\\u0048\\u0065\\u006c\\u006c\\u006f\"\n");
assert(d20.text == "Hello", "unicode \\uXXXX");

// Test 21: Unicode escape \UXXXXXXXX
let d21 = yaml_parse("text: \"\\U00000041\\U00000042\\U00000043\"\n");
assert(d21.text == "ABC", "unicode \\UXXXXXXXX");

// Test 22: Unicode escape UTF-8 multibyte (emoji)
let d22 = yaml_parse("emoji: \"\\U0001F600\"\n");
assert(typeof(d22.emoji) == "string", "unicode emoji type");
assert(len(d22.emoji) > 0, "unicode emoji length");

// Test 23: JSON compatibility - nested objects
let d23 = yaml_parse("{\"a\": {\"b\": {\"c\": 123}}}\n");
assert(d23.a.b.c == 123, "JSON nested objects");

// Test 24: JSON compatibility - arrays
let d24 = yaml_parse("{\"items\": [1, 2, 3, 4, 5]}\n");
assert(len(d24.items) == 5, "JSON array length");
assert(d24.items[2] == 3, "JSON array element");

// Test 25: JSON compatibility - mixed
let d25 = yaml_parse("{\"name\": \"test\", \"count\": 42, \"active\": true, \"data\": null}\n");
assert(d25.name == "test", "JSON mixed name");
assert(d25.count == 42, "JSON mixed count");
assert(d25.active == true, "JSON mixed active");
assert(d25.data == nil, "JSON mixed null");

// Test 26: Anchor and alias with tag
let d26 = yaml_parse("value: &anchor !!str 100\nref: *anchor\n");
assert(d26.value == "100", "anchor with tag value");
assert(d26.ref == "100", "anchor with tag ref");

// Test 27: Multiline with explicit indent and folding
let d27 = yaml_parse("text: >2\n  line one\n  line two\n");
assert(contains(d27.text, "line"), "folded explicit indent");

// Test 28: Empty document after directives
let d28 = yaml_parse("%YAML 1.2\n---\n");
assert(d28 == nil, "empty after directives");

// Test 29: Multiple documents with directives
let docs29 = yaml_parse_all("%YAML 1.2\n---\nx: 1\n---\ny: 2\n");
assert(len(docs29) == 2, "directives multi-doc count");

// Test 30: Anchor in list
let d30 = yaml_parse("- &item one\n- *item\n");
assert(len(d30) == 2, "anchor list length");
assert(d30[0] == "one", "anchor list value");
assert(d30[1] == "one", "anchor list alias");

// Test 31: Complex nested merge
let d31 = yaml_parse("defaults: &defaults\n  timeout: 30\n  retry: 3\nservice1:\n  <<: *defaults\n  name: s1\nservice2:\n  <<: *defaults\n  name: s2\n  timeout: 60\n");
assert(d31.service1.timeout == 30, "nested merge s1 timeout");
assert(d31.service1.retry == 3, "nested merge s1 retry");
assert(d31.service2.timeout == 60, "nested merge s2 timeout override");
assert(d31.service2.retry == 3, "nested merge s2 retry");

// Test 32: Tag with flow collection
let d32 = yaml_parse("nums: !!seq [1, 2, 3]\n");
assert(len(d32.nums) == 3, "tag flow list length");

// Test 33: Explicit key with complex value
let d33 = yaml_parse("? key\n: [a, b, c]\n");
assert(len(d33.key) == 3, "explicit key complex value");

// Test 34: Local tag (custom tag - treated as plain value)
let d34 = yaml_parse("value: !custom data\n");
assert(d34.value == "data", "local tag");

// Test 35: Standard tag long form
let d35 = yaml_parse("value: tag:yaml.org,2002:str 999\n");
// This might not parse correctly without full tag expansion, but shouldn't error
assert(typeof(d35) == "map", "long form tag");

// Test 36: Boolean variants with yes/no
let d36 = yaml_parse("a: yes\nb: no\nc: on\nd: off\n");
assert(d36.a == true, "bool yes");
assert(d36.b == false, "bool no");
assert(d36.c == true, "bool on");
assert(d36.d == false, "bool off");

// Test 37: Hex number with tag
let d37 = yaml_parse("val: !!int 0xFF\n");
assert(d37.val == 255, "hex with tag");

// Test 38: Octal number with tag  
let d38 = yaml_parse("val: !!int 0o777\n");
assert(d38.val == 511, "octal with tag");

// Test 39: Special float values
let d39 = yaml_parse("a: .inf\nb: -.inf\nc: .nan\n");
assert(typeof(d39.a) == "float", "inf type");
assert(typeof(d39.b) == "float", "-inf type");
assert(typeof(d39.c) == "float", "nan type");

// Test 40: Merge with override
let d40 = yaml_parse("base: &base\n  a: 1\n  b: 2\nderived:\n  <<: *base\n  a: 99\n");
assert(d40.derived.a == 99, "merge override works");
assert(d40.derived.b == 2, "merge non-override");

// Test 41: Hex escape \xHH (YAML 1.2.2)
let d41 = yaml_parse("text: \"Hello\\x20World\"\n");
assert(d41.text == "Hello World", "hex escape space");

// Test 42: Hex escape for tab
let d42 = yaml_parse("text: \"Tab\\x09here\"\n");
assert(d42.text == "Tab\there", "hex escape tab");

// Test 43: Next line escape \N (U+0085)
let d43 = yaml_parse("text: \"Line1\\NLine2\"\n");
assert(len(d43.text) == 12, "next line escape length");
assert(contains(d43.text, "Line1"), "next line escape has Line1");
assert(contains(d43.text, "Line2"), "next line escape has Line2");

// Test 44: Non-breaking space \_ (U+00A0)
let d44 = yaml_parse("text: \"No\\_break\"\n");
assert(len(d44.text) == 9, "non-breaking space length");
assert(contains(d44.text, "No"), "NBSP has No");
assert(contains(d44.text, "break"), "NBSP has break");

// Test 45: Line separator \L (U+2028)
let d45 = yaml_parse("text: \"Line1\\LLine2\"\n");
assert(len(d45.text) == 13, "line separator length");
assert(contains(d45.text, "Line1"), "LS has Line1");
assert(contains(d45.text, "Line2"), "LS has Line2");

// Test 46: Paragraph separator \P (U+2029)
let d46 = yaml_parse("text: \"Para1\\PPara2\"\n");
assert(len(d46.text) == 13, "paragraph separator length");
assert(contains(d46.text, "Para1"), "PS has Para1");
assert(contains(d46.text, "Para2"), "PS has Para2");

// Test 47: Mixed escape sequences
let d47 = yaml_parse("text: \"\\x41\\u0042\\x43\"\n");
assert(d47.text == "ABC", "mixed hex and unicode escapes");

// Test 48: Low byte hex escape
let d48 = yaml_parse("text: \"Null\\x00byte\"\n");
assert(len(d48.text) == 9, "null byte escape length");

// Test 49: High byte hex escape
let d49 = yaml_parse("text: \"\\xFF\"\n");
assert(len(d49.text) > 0, "high byte hex escape");

// Test 50: All special Unicode escapes together
let d50 = yaml_parse("text: \"A\\NB\\_C\\LD\\PE\"\n");
assert(contains(d50.text, "A"), "multi-special escapes start");
assert(contains(d50.text, "E"), "multi-special escapes end");

print("All advanced YAML tests passed!");
