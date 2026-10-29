let order = [];
fn deferred_returns() {
    defer { push(order, "first"); }
    defer { push(order, "second"); return ["replacement"]; }
    return ["original"];
}
let returned = deferred_returns();
assert(returned[0] == "replacement", "defer replaces return value");
assert(order[0] == "second" && order[1] == "first", "remaining defers still run");

fn deferred_throws() {
    defer { push(order, "after throw"); }
    defer { throw {msg: "cleanup error"}; }
    throw {msg: "original error"};
}
try { deferred_throws(); } catch (e) {
    assert(e.msg == "cleanup error", "defer replaces thrown value");
}
assert(order[2] == "after throw", "remaining defers run after another defer throws");

fn finally_replaces_runtime_error() {
    try { let bad = 1 / 0; } catch (e) { return "caught"; }
    finally { return "cleanup"; }
}
assert(finally_replaces_runtime_error() == "cleanup", "finally return replaces runtime error");

let count = 0;
for x in [1, 2, 3] {
    try { if (x == 1) { continue; } break; } catch (e) {}
    finally { count += 1; }
}
assert(count == 2, "finally runs on continue and break");
