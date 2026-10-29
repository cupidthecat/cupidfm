// String quality-of-life tests

let s = "  Hello World  \n";
assert(trim(s) == "Hello World", "trim");
assert(ltrim(s) == "Hello World  \n", "ltrim");
assert(rtrim(s) == "  Hello World", "rtrim");

assert(lower("HeLLo") == "hello", "lower");
assert(upper("HeLLo") == "HELLO", "upper");

assert(starts_with("hello.txt", "hello"), "starts_with");
assert(ends_with("hello.txt", ".txt"), "ends_with");

assert(contains("hello world", "world"), "contains string");

let lines = split_lines("a\n b\r\n");
assert(len(lines) == 3, "split_lines count");
assert(lines[0] == "a", "split_lines line0");
assert(lines[1] == " b", "split_lines line1");
assert(lines[2] == "", "split_lines line2");

print("string_qol ok");
