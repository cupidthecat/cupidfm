assert(str_repeat("", 9223372036854775807) == "", "empty repeat terminates");
assert(str_repeat("ab", 3) == "ababab", "repeat content");
assert(str_repeat("abc", 0) == "", "zero repeat");
assert(str_repeat("abc", -1) == "", "negative repeat");
