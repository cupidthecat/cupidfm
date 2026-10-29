// const bindings

const a = 10;
assert(a == 10, "const value");

const [x, y] = [1, 2];
assert(x == 1 && y == 2, "const list destructuring");

const {name, age} = {"name": "Ada", "age": 36};
assert(name == "Ada" && age == 36, "const map destructuring");

print("const_bindings ok");
