// Nullish coalescing and optional chaining example

let user = nil;
let name = user?.name ?? "Guest";
print("Hello " + name);

let cfg = {"timeout": 5000};
let timeout = cfg?.timeout ?? 1000;
print("timeout=" + timeout);
