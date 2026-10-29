// JSON example script

let payload = {
  "user": {"id": 42, "name": "Frank"},
  "flags": [true, false, true],
  "meta": nil
};

let encoded = json_stringify(payload);
print("encoded:", encoded);

let decoded = json_parse(encoded);
assert(decoded.user.id == 42, "json example id");
assert(decoded.user.name == "Frank", "json example name");
assert(decoded.flags[0] == true, "json example flags");
assert(decoded.meta == nil, "json example nil");

print("json_example ok");
