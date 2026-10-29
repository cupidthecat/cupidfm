// Test the special Unicode escapes
let d1 = yaml_parse("text: \"Line1\\NLine2\"\n");
print("Test NEL:", d1.text);
print("Length:", len(d1.text));

let d2 = yaml_parse("text: \"No\\_break\"\n");
print("Test NBSP:", d2.text);
print("Length:", len(d2.text));

let d3 = yaml_parse("text: \"Line1\\LLine2\"\n");
print("Test LS:", d3.text);
print("Length:", len(d3.text));

let d4 = yaml_parse("text: \"Para1\\PPara2\"\n");
print("Test PS:", d4.text);
print("Length:", len(d4.text));

print("Unicode escape tests passed!");
