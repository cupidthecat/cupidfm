// Test just the hex escape
let d1 = yaml_parse("text: \"Hello\\x20World\"\n");
print("Test 1:", d1.text);
assert(d1.text == "Hello World", "test 1");

let d2 = yaml_parse("text: \"Tab\\x09here\"\n");
print("Test 2:", d2.text);
assert(d2.text == "Tab\there", "test 2");

print("Simple tests passed!");
