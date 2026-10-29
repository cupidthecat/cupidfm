// Default parameter values

// Single default
fn greet(name, greeting = "Hello") {
    return greeting + ", " + name + "!";
}
assert(greet("Alice") == "Hello, Alice!", "single default used");
assert(greet("Bob", "Hi") == "Hi, Bob!", "single default overridden");

// Multiple defaults
fn make_point(x = 0, y = 0, z = 0) {
    return [x, y, z];
}
assert(make_point()[0] == 0, "all defaults");
assert(make_point()[1] == 0, "all defaults y");
assert(make_point(1)[0] == 1, "one arg");
assert(make_point(1)[1] == 0, "one arg, default y");
assert(make_point(1, 2)[2] == 0, "two args, default z");
assert(make_point(1, 2, 3)[2] == 3, "all args");

// Default referencing earlier param
fn repeat_str(s, times = 1) {
    let result = "";
    for i in range(times) {
        result = result + s;
    }
    return result;
}
assert(repeat_str("x") == "x", "default times=1");
assert(repeat_str("x", 3) == "xxx", "explicit times=3");

// Anonymous function with defaults
let add = fn(a, b = 10) {
    return a + b;
};
assert(add(5) == 15, "anon fn default");
assert(add(5, 3) == 8, "anon fn explicit");

// Default is expression evaluated at call time
let counter = 0;
fn next_id(prefix = "id") {
    counter = counter + 1;
    return prefix + to_str(counter);
}
assert(next_id() == "id1", "default call 1");
assert(next_id() == "id2", "default call 2");
assert(next_id("user") == "user3", "explicit prefix");

// Nil explicitly passed uses nil, not default
fn maybe(val = "default") {
    return val;
}
assert(maybe() == "default", "missing uses default");
assert(maybe(nil) == nil, "explicit nil stays nil");

print("default_params ok");
