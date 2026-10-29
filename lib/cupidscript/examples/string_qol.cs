// String quality-of-life example

let raw = "  Hello CupidScript  \r\n";
print(trim(raw));
print(ltrim(raw));
print(rtrim(raw));

print(lower("HeLLo"));
print(upper("HeLLo"));

print(starts_with("hello.txt", "hello"));
print(ends_with("hello.txt", ".txt"));

print(contains("hello world", "world"));

let lines = split_lines("a\n b\r\n");
print(lines);
