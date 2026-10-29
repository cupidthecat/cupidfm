// String utility tests

assert(str_contains("hello", "ell"), "str_contains");
assert(!str_contains("hello", "xyz"), "str_contains false");

assert(str_count("aaaa", "aa") == 2, "str_count non-overlap");
assert(str_count("aaaa", "") == 0, "str_count empty sub");

assert(str_pad_start("7", 3, "0") == "007", "str_pad_start");
assert(str_pad_end("7", 3, "0") == "700", "str_pad_end");
assert(str_pad_start("hi", 2, "0") == "hi", "str_pad_start no pad");

assert(str_reverse("abc") == "cba", "str_reverse");
assert(str_reverse("") == "", "str_reverse empty");

print("string_utils ok");
