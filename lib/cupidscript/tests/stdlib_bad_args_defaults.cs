// Stdlib "bad args" behavior: many functions return a default value rather than error

assert(is_nil(typeof()), "typeof() -> nil");
assert(is_nil(typeof(1, 2)), "typeof(too many) -> nil");

assert(is_nil(getenv(123)), "getenv(non-string) -> nil");

assert(len() == 0, "len() -> 0");
assert(len(123) == 0, "len(non-collection) -> 0");

let m = {"a": 1};
assert(is_nil(mget(m, 123)), "mget(map, non-string) -> nil");
assert(!mhas(m, 123), "mhas(map, non-string) -> false");
assert(!mdel(m, 123), "mdel(map, non-string) -> false");

let ks = keys(123);
assert(is_list(ks) && len(ks) == 0, "keys(non-map) -> []");

let vs = values(123);
assert(is_list(vs) && len(vs) == 0, "values(non-map) -> []");

let it = items(123);
assert(is_list(it) && len(it) == 0, "items(non-map) -> []");

assert(is_nil(path_join(1, 2)), "path_join(bad args) -> nil");
assert(is_nil(path_dirname(1)), "path_dirname(bad args) -> nil");
assert(is_nil(path_basename(1)), "path_basename(bad args) -> nil");
assert(is_nil(path_ext(1)), "path_ext(bad args) -> nil");

print("stdlib_bad_args_defaults ok");
