// Test only the features up to test 17 (before merge operator)
print("Test 1-17 only (no merge operator)");

let d1 = yaml_parse("text: |-\n  line1\n  line2\n");
assert(d1.text == "line1\nline2", "test 1");

let d7 = yaml_parse("num: !!str 123\n");
assert(d7.num == "123", "test 7");

let d13 = yaml_parse("%YAML 1.2\n---\nkey: value\n");
assert(d13.key == "value", "test 13");

let docs14 = yaml_parse_all("---\na: 1\n---\nb: 2\n");
assert(len(docs14) == 2, "test 14");

let d16 = yaml_parse("? key1\n: value1\n");
assert(d16.key1 == "value1", "test 16");

print("All tests passed (no merge operator)");
