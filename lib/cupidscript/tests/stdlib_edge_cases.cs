// Edge cases for stdlib functions

// str_replace: empty "old" should return original string
assert(str_replace("abc", "", "x") == "abc", "str_replace empty old");

// str_split: empty separator returns [s]
let one = str_split("abc", "");
assert(len(one) == 1, "str_split empty sep len");
assert(one[0] == "abc", "str_split empty sep item");

// path_dirname edge cases
assert(path_dirname("abc") == ".", "path_dirname no slash");
assert(path_dirname("/a") == "/", "path_dirname root child");

// path_ext edge cases
assert(path_ext("noext") == "", "path_ext none");
assert(path_ext(".bashrc") == "", "path_ext dotfile");

// sleep: non-positive should be a no-op (no error)
await sleep(0);
await sleep(-1);

// fmt: empty format string
assert(fmt("") == "", "fmt empty");

print("stdlib_edge_cases ok");
