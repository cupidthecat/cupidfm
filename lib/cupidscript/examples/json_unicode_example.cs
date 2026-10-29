// examples/json_unicode_example.cs
// Demonstrate JSON Unicode and surrogate pair handling

let obj = {
  ascii: "A",
  latin: "é",
  bmp: "€",
  astral: "𝄞" // U+1D11E G clef
};

let json = json_stringify(obj);
print("JSON:", json);

let parsed = json_parse(json);
print("ascii:", parsed["ascii"]);
print("latin:", parsed["latin"]);
print("bmp:", parsed["bmp"]);
print("astral:", parsed["astral"]);

assert(parsed["ascii"] == "A");
assert(parsed["latin"] == "é");
assert(parsed["bmp"] == "€");
assert(parsed["astral"] == "𝄞");

print("json_unicode_example ok");
