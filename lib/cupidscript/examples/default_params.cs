// Default parameter examples

fn greet(name, greeting = "Hello") {
    return greeting + ", " + name + "!";
}

print(greet("Alice"));
print(greet("Bob", "Hi"));

let add = fn(a, b = 10) { return a + b; };
print(add(5));
print(add(5, 3));

let counter = 0;
fn next_id(prefix = "id") {
    counter = counter + 1;
    return prefix + to_str(counter);
}

print(next_id());
print(next_id());
print(next_id("user"));
