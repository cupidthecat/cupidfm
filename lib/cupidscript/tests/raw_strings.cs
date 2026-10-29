let s1 = `hello\nworld`;
assert(s1 == "hello\\nworld", "raw strings do not escape backslashes");

let s2 = `line1
line2`;
assert(s2 == "line1\nline2" || s2 == "line1\r\nline2", "raw strings can span lines");

let s3 = `He said "hi"`;
assert(s3 == "He said \"hi\"", "raw strings keep quotes");

let s4 = `C:\temp\file`;
assert(s4 == "C:\\temp\\file", "raw strings keep backslashes");

print("raw_strings ok");
