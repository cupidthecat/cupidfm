// Test high byte
let d = yaml_parse("text: \"\\xFF\"\n");
print("Result:", d.text);
print("Length:", len(d.text));
print("Test passed!");
