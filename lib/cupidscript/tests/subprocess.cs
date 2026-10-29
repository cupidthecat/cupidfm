// subprocess: run command and capture output

let r = subprocess("echo", ["hello"]);
assert(r != nil, "subprocess result");
assert(r.code == 0, "exit code");
assert(str_contains(r.out, "hello"), "output contains hello");

print("subprocess ok");
