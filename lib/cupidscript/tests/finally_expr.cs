// finally expression

let log = "";
fn append(x) { log = log + x; }

try {
  throw error("boom", "E_TEST");
} catch (e) {
  assert(is_error(e), "caught error object");
  assert(e.msg == "boom", "error msg");
  assert(e.code == "E_TEST", "error code");
} finally append("F");

assert(log == "F", "finally expr runs");

print("finally_expr ok");
