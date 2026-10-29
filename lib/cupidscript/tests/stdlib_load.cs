// Stdlib load() executes a file (no module caching)

let load_count = 0;

assert(load("_load_fixture.cs"), "load returns true");
assert(load_count == 1, "fixture ran once");

assert(load("_load_fixture.cs"), "load returns true again");
assert(load_count == 2, "fixture ran twice");

// loaded functions become available in globals
assert(loaded_value() == 123, "loaded function callable");

print("stdlib_load ok");
