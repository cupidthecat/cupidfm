// Regex stdlib tests

assert(regex_is_match("[0-9]+", "abc123def"), "regex_is_match");
assert(!regex_match("[0-9]+", "abc123def"), "regex_match non-full");
assert(regex_match("^[a-z]+[0-9]+[a-z]+[0-9]+$", "abc123def456"), "regex_match full");

let m = regex_find("([0-9]+)", "foo-123-bar");
assert(m != nil, "regex_find not nil");
assert(m.start == 4, "regex_find start");
assert(m.end == 7, "regex_find end");
assert(m["match"] == "123", "regex_find match");
assert(len(m.groups) == 1, "regex_find groups len");
assert(m.groups[0] == "123", "regex_find group0");

let all = regex_find_all("[0-9]+", "a1 b22 c333");
assert(len(all) == 3, "regex_find_all len");
assert(all[0]["match"] == "1", "regex_find_all first");
assert(all[1]["match"] == "22", "regex_find_all second");
assert(all[2]["match"] == "333", "regex_find_all third");

let r = regex_replace("[0-9]+", "a1b22c", "#");
assert(r == "a#b#c", "regex_replace");

print("regex ok");
