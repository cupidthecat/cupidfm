fn recursive(n) { if (n == 0) { return 0; } return recursive(n - 1); }
assert(recursive(5) == 0, "recursive globals");
fn factory() {
    fn local(n) { if (n == 0) { return 7; } return local(n - 1); }
    return local;
}
let callback = factory();
assert(callback(3) == 7, "recursive closure");
