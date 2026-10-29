// Map field access + indexing semantics

let m = {"a": 1, "b": 2};

// field access is sugar for map["field"]
assert(m.a == 1, "field get a");
assert(m.b == 2, "field get b");

// missing keys should yield nil
assert(is_nil(m.missing), "field get missing -> nil");
assert(is_nil(m["missing"]), "index get missing -> nil");

// index assignment on map
m["c"] = 3;
assert(m.c == 3, "index set then field get");

print("map_fields ok");
