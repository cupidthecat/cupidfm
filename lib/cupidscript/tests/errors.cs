// Error objects + try/catch + throw

let e = error("boom", "E_BOOM");
assert(is_error(e), "is_error");
assert(format_error(e) == "[E_BOOM] boom", "format_error");

let caught = nil;
try {
  throw e;
} catch (x) {
  caught = x;
}

assert(is_error(caught), "catch value");
assert(format_error(caught) == "[E_BOOM] boom", "caught formatting");

print("errors ok");
