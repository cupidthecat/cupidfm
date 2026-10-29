// try/catch/finally behavior

let log = "";
fn append(x) { log = log + x; }

// finally runs on normal completion
try {
  append("A");
} catch (e) {
  append("B");
} finally {
  append("C");
}
assert(log == "AC", "finally after normal completion");

// finally runs on throw
log = "";
try {
  throw "x";
} catch (e) {
  append("B");
} finally {
  append("C");
}
assert(log == "BC", "finally after throw");

// finally runs on return
log = "";
fn f() {
  try {
    append("T");
    return 1;
  } catch (e) {
    append("C");
  } finally {
    append("F");
  }
}
let r = f();
assert(r == 1, "return passes through finally");
assert(log == "TF", "finally after return");

// finally runs even if catch rethrows
log = "";
try {
  try {
    throw "x";
  } catch (e) {
    append("C");
    throw "y";
  } finally {
    append("F");
  }
} catch (e2) {
  append("O");
}
assert(log == "CFO", "finally on rethrow");

print("trycatch_finally ok");
