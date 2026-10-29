// Demonstrates: collecting list/map reference cycles (v0 cycle collector)

let m = map();
m["self"] = m;

let xs = list();
xs[0] = xs;

// Drop external references (cycles remain due to refcounting)
m = nil;
xs = nil;

print("collected =", gc());

