// tests/json_unicode_surrogates.cs
// Test JSON parsing and stringifying with Unicode and surrogate pairs

let json = `{"ascii":"A","latin":"\u00E9","bmp":"\u20AC","astral":"\uD834\uDD1E"}`;
let obj = json_parse(json);
print("ascii:", obj["ascii"]);
print("latin:", obj["latin"]);
print("bmp:", obj["bmp"]);
print("astral:", obj["astral"]);

assert(obj["ascii"] == "A", "ASCII char");

// Compare bytes for Latin-1 char
let latin_bytes = bytes(obj["latin"]);
print("latin bytes:", latin_bytes[0], latin_bytes[1]);
assert(latin_bytes[0] == 195, "Latin-1 byte 0 (0xC3)");
assert(latin_bytes[1] == 169, "Latin-1 byte 1 (0xA9)");

// Compare bytes for BMP char (Euro sign U+20AC)
let bmp_bytes = bytes(obj["bmp"]);
print("bmp bytes:", bmp_bytes[0], bmp_bytes[1], bmp_bytes[2]);
assert(bmp_bytes[0] == 226, "BMP byte 0 (0xE2)");
assert(bmp_bytes[1] == 130, "BMP byte 1 (0x82)");
assert(bmp_bytes[2] == 172, "BMP byte 2 (0xAC)");

// Compare bytes for astral char (G clef U+1D11E)
let astral_bytes = bytes(obj["astral"]);
print("astral bytes:", astral_bytes[0], astral_bytes[1], astral_bytes[2], astral_bytes[3]);
assert(astral_bytes[0] == 240, "Astral byte 0 (0xF0)");
assert(astral_bytes[1] == 157, "Astral byte 1 (0x9D)");
assert(astral_bytes[2] == 132, "Astral byte 2 (0x84)");
assert(astral_bytes[3] == 158, "Astral byte 3 (0x9E)");

// Round-trip
let out = json_stringify(obj);
let obj2 = json_parse(out);
assert(obj2["ascii"] == "A", "Round-trip ASCII");

// Note: Invalid surrogates (lone high/low) cause json_parse to throw an error
// This is correct RFC 8259 behavior

print("json_unicode_surrogates ok");
