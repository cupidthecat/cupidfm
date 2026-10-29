// contains(map, key) accepts any key type

let m = {"a": 1};
assert(contains(m, 123) == false, "contains map non-string key");
print("negative_contains_map_key_type ok");
