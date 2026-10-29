// Generalized map keys example

let cache = {};
let key = ["user", 42];

cache[key] = {name: "Ada", role: "admin"};
print(cache[key].name);        // Ada

// int/float keys compare equal
cache[1] = "one";
print(cache[1.0]);             // one

// Use mget/mset with non-string keys
mset(cache, true, "yes");
print(mget(cache, true));      // yes

print("keys:", keys(cache));
