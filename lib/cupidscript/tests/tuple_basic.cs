// Basic tuple tests

// Positional tuple creation
let coords = (10, 20, 30)
print("Positional tuple:", coords)
if (typeof(coords) != "tuple") { throw "Should be tuple type" }

// Positional tuple access
print("coords[0]:", coords[0])
print("coords[1]:", coords[1])
print("coords[2]:", coords[2])
if (coords[0] != 10) { throw "First element should be 10" }
if (coords[1] != 20) { throw "Second element should be 20" }
if (coords[2] != 30) { throw "Third element should be 30" }

// Named tuple creation
let point = (x: 100, y: 200)
print("Named tuple:", point)
if (typeof(point) != "tuple") { throw "Should be tuple type" }

// Named tuple field access
print("point.x:", point.x)
print("point.y:", point.y)
if (point.x != 100) { throw "x should be 100" }
if (point.y != 200) { throw "y should be 200" }

// Mixed access (named tuples can also be accessed positionally)
print("point[0]:", point[0])
print("point[1]:", point[1])
if (point[0] != 100) { throw "First element should be 100" }
if (point[1] != 200) { throw "Second element should be 200" }

// Single element tuple (with trailing comma)
let single = (42,)
print("Single element tuple:", single)
if (typeof(single) != "tuple") { throw "Should be tuple type" }
if (single[0] != 42) { throw "Element should be 42" }

// Empty tuple
let empty = ()
print("Empty tuple:", empty)
if (typeof(empty) != "tuple") { throw "Should be tuple type" }

// Tuple with different types
let mixed = ("hello", 42, true, nil)
print("Mixed type tuple:", mixed)
if (mixed[0] != "hello") { throw "First should be string" }
if (mixed[1] != 42) { throw "Second should be int" }
if (mixed[2] != true) { throw "Third should be bool" }
if (mixed[3] != nil) { throw "Fourth should be nil" }

print("All basic tuple tests passed!")
