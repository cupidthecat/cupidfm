// getenv / now_ms / sleep

let p = getenv("PATH");
assert(is_nil(p) || is_string(p), "getenv returns nil or string");

let t1 = now_ms();
assert(is_int(t1), "now_ms returns int");

// keep sleep tiny; just ensure it doesn't error
await sleep(1);
let t2 = now_ms();
assert(is_int(t2), "now_ms still int");
assert(t2 >= t1, "monotonic-ish");

print("stdlib_time_env ok");
