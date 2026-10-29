// export statement example

export answer = 42;
export hello = fn(name) { return "hello " + name; };

print("answer:", exports.answer);
print(exports.hello("world"));

// when required:
// let lib = require("exports.cs");
// print(lib.answer);
// print(lib.hello("world"));
