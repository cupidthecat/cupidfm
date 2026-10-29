// YAML parsing and stringify tests - basic functionality

// Test 1: Simple string
let d1 = yaml_parse("name: John\n");
assert(d1.name == "John", "simple string");

// Test 2: Integer
let d2 = yaml_parse("age: 30\n");
assert(d2.age == 30, "simple integer");

// Test 3: Float
let d3 = yaml_parse("score: 95.5\n");
assert(d3.score == 95.5, "simple float");

// Test 4: Boolean true
let d4 = yaml_parse("active: true\n");
assert(d4.active == true, "boolean true");

// Test 5: Boolean false
let d5 = yaml_parse("active: false\n");
assert(d5.active == false, "boolean false");

// Test 6: Null
let d6 = yaml_parse("value: null\n");
assert(d6.value == nil, "null value");

// Test 7: Multiple keys
let d7 = yaml_parse("a: 1\nb: 2\nc: 3\n");
assert(d7.a == 1, "multiple keys - a");
assert(d7.b == 2, "multiple keys - b");
assert(d7.c == 3, "multiple keys - c");

// Test 8: Quoted string (single)
let d8 = yaml_parse("text: 'hello world'\n");
assert(d8.text == "hello world", "single quoted");

// Test 9: Quoted string (double)
let d9 = yaml_parse("text: \"hello world\"\n");
assert(d9.text == "hello world", "double quoted");

// Test 10: Hex number
let d10 = yaml_parse("val: 0x10\n");
assert(d10.val == 16, "hex number");

// Test 11: Octal number
let d11 = yaml_parse("val: 0o10\n");
assert(d11.val == 8, "octal number");

// Test 12: Flow list
let d12 = yaml_parse("items: [1, 2, 3]\n");
assert(len(d12.items) == 3, "flow list length");
assert(d12.items[0] == 1, "flow list item 0");
assert(d12.items[2] == 3, "flow list item 2");

// Test 13: Empty flow list
let d13 = yaml_parse("items: []\n");
assert(len(d13.items) == 0, "empty flow list");

// Test 14: Document marker
let d14 = yaml_parse("---\nname: Alice\n");
assert(d14.name == "Alice", "document with --- marker");

// Test 15: Comments
let d15 = yaml_parse("# comment\nname: Bob\n");
assert(d15.name == "Bob", "with comment");

// Test 16: Empty document
let d16 = yaml_parse("");
assert(d16 == nil, "empty document");

// Test 17: Stringify simple map
let data17 = {"name": "John", "age": 30};
let yaml17 = yaml_stringify(data17);
assert(typeof(yaml17) == "string", "stringify returns string");
assert(contains(yaml17, "name"), "stringify contains key");
assert(contains(yaml17, "John"), "stringify contains value");

// Test 18: Stringify with numbers
let data18 = {"x": 10, "y": 20.5, "z": true};
let yaml18 = yaml_stringify(data18);
assert(contains(yaml18, "10"), "stringify has integer");
assert(contains(yaml18, "20.5"), "stringify has float");
assert(contains(yaml18, "true"), "stringify has boolean");

// Test 19: Stringify empty map
let yaml19 = yaml_stringify({});
assert(yaml19 == "{}", "stringify empty map");

// Test 20: Stringify empty list
let yaml20 = yaml_stringify({"items": []});
assert(contains(yaml20, "[]"), "stringify empty list");

// Test 21: Roundtrip simple values
let orig21 = {"a": 1, "b": 2, "c": 3};
let yaml_text21 = yaml_stringify(orig21);
let back21 = yaml_parse(yaml_text21);
assert(back21.a == 1, "roundtrip a");
assert(back21.b == 2, "roundtrip b");
assert(back21.c == 3, "roundtrip c");

// Test 22: Stringify list (basic)
let data22 = {"tags": ["x", "y"]};
let yaml22 = yaml_stringify(data22);
assert(contains(yaml22, "tags"), "stringify list key");
assert(contains(yaml22, "- x"), "stringify list item");

// Test 23: Stringify custom indent
let yaml23a = yaml_stringify({"a": 1}, 2);
let yaml23b = yaml_stringify({"a": 1}, 4);
assert(typeof(yaml23a) == "string", "custom indent 2");
assert(typeof(yaml23b) == "string", "custom indent 4");

// Test 24: Parse plain scalars
let d24 = yaml_parse("plain: hello\n");
assert(d24.plain == "hello", "plain scalar");

// Test 25: Parse negative numbers
let d25 = yaml_parse("neg: -42\n");
assert(d25.neg == -42, "negative number");

print("All YAML tests passed!");
