// Quick test for new escape sequences
let d1 = yaml_parse("text: \"Line1\\NLine2\"\n");
print("NEL Result:", d1.text);
print("NEL Length:", len(d1.text));

let d2 = yaml_parse("text: \"No\\_break\"\n");
print("NBSP Result:", d2.text);
print("NBSP Length:", len(d2.text));

let d3 = yaml_parse("text: \"Line1\\LLine2\"\n");
print("LS Result:", d3.text);
print("LS Length:", len(d3.text));

let d4 = yaml_parse("text: \"Para1\\PPara2\"\n");
print("PS Result:", d4.text);
print("PS Length:", len(d4.text));

let d5 = yaml_parse("text: \"Hello\\x20World\"\n");
print("Hex Result:", d5.text);
assert(d5.text == "Hello World", "hex escape works");

let d6 = yaml_parse("text: \"\\x41\\u0042\\x43\"\n");
print("Mixed Result:", d6.text);
assert(d6.text == "ABC", "mixed escapes work");

print("Basic escape tests passed!");
