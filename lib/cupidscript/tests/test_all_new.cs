print("Testing all new escapes...");

// Test 41: Hex escape \xHH (YAML 1.2.2)
let d41 = yaml_parse("text: \"Hello\\x20World\"\n");
assert(d41.text == "Hello World", "hex escape space");
print("41 passed");

// Test 42: Hex escape for tab
let d42 = yaml_parse("text: \"Tab\\x09here\"\n");
assert(d42.text == "Tab\there", "hex escape tab");
print("42 passed");

// Test 43: Next line escape \N (U+0085)
let d43 = yaml_parse("text: \"Line1\\NLine2\"\n");
assert(len(d43.text) == 12, "next line escape length");
assert(contains(d43.text, "Line1"), "next line escape has Line1");
assert(contains(d43.text, "Line2"), "next line escape has Line2");
print("43 passed");

// Test 44: Non-breaking space \_ (U+00A0)
let d44 = yaml_parse("text: \"No\\_break\"\n");
assert(len(d44.text) == 9, "non-breaking space length");
assert(contains(d44.text, "No"), "NBSP has No");
assert(contains(d44.text, "break"), "NBSP has break");
print("44 passed");

// Test 45: Line separator \L (U+2028)
let d45 = yaml_parse("text: \"Line1\\LLine2\"\n");
assert(len(d45.text) == 13, "line separator length");
assert(contains(d45.text, "Line1"), "LS has Line1");
assert(contains(d45.text, "Line2"), "LS has Line2");
print("45 passed");

// Test 46: Paragraph separator \P (U+2029)
let d46 = yaml_parse("text: \"Para1\\PPara2\"\n");
assert(len(d46.text) == 13, "paragraph separator length");
assert(contains(d46.text, "Para1"), "PS has Para1");
assert(contains(d46.text, "Para2"), "PS has Para2");
print("46 passed");

// Test 47: Mixed escape sequences
let d47 = yaml_parse("text: \"\\x41\\u0042\\x43\"\n");
assert(d47.text == "ABC", "mixed hex and unicode escapes");
print("47 passed");

// Test 48: Low byte hex escape
let d48 = yaml_parse("text: \"Null\\x00byte\"\n");
assert(len(d48.text) == 9, "null byte escape length");
print("48 passed");

// Test 49: High byte hex escape
let d49 = yaml_parse("text: \"\\xFF\"\n");
assert(len(d49.text) > 0, "high byte hex escape");
print("49 passed");

// Test 50: All special Unicode escapes together
let d50 = yaml_parse("text: \"A\\NB\\_C\\LD\\PE\"\n");
assert(contains(d50.text, "A"), "multi-special escapes start");
assert(contains(d50.text, "E"), "multi-special escapes end");
print("50 passed");

print("All tests passed!");
