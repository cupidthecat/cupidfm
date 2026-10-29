// JSON parse/stringify tests

let src = "{\"a\":1,\"b\":true,\"c\":null,\"d\":[1,2,3],\"e\":\"hi\"}";
let v = json_parse(src);
assert(v.a == 1, "json_parse int");
assert(v.b == true, "json_parse bool");
assert(v.c == nil, "json_parse null");
assert(v.d[1] == 2, "json_parse array");
assert(v.e == "hi", "json_parse string");

let s2 = json_stringify(v);
let v2 = json_parse(s2);
assert(v2.a == 1 && v2.d[2] == 3 && v2.e == "hi", "json roundtrip");

let arr = json_parse("[1,2,3]");
assert(len(arr) == 3 && arr[0] == 1 && arr[2] == 3, "json array parse");

let s3 = json_stringify([1, true, nil, "x"]);
let v3 = json_parse(s3);
assert(v3[0] == 1 && v3[1] == true && v3[2] == nil && v3[3] == "x", "json array roundtrip");

print("json ok");
