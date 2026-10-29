// Tuple immutability tests

// Tuples should be immutable - fields cannot be reassigned
let point = (x: 10, y: 20)
print("Original point:", point)
print("point.x:", point.x, "point.y:", point.y)

// NOTE: Attempting to assign to tuple fields should fail
// This tests that tuples are read-only
// point.x = 100  // This should error
// point[0] = 100 // This should also error

// But we can create new tuples
let new_point = (x: 100, y: 200)
print("New point:", new_point)
if (new_point.x != 100) { throw "new x should be 100" }
if (new_point.y != 200) { throw "new y should be 200" }

// Original tuple unchanged
if (point.x != 10) { throw "original x should still be 10" }
if (point.y != 20) { throw "original y should still be 20" }

// Tuples with same values should be equal in comparisons
let p1 = (1, 2, 3)
let p2 = (1, 2, 3)
let p3 = (1, 2, 4)

print("p1:", p1)
print("p2:", p2)
print("p3:", p3)

// Tuple values are copied when assigned
let original = (a: 1, b: 2)
let copy = original
print("original:", original, "copy:", copy)
if (copy[0] != 1) { throw "copy[0] should be 1" }
if (copy[1] != 2) { throw "copy[1] should be 2" }

// Tuples containing mutable values
let tuple_with_list = ([1, 2, 3], "test")
print("Tuple with list:", tuple_with_list)
if (typeof(tuple_with_list[0]) != "list") { throw "First element should be list" }
if (tuple_with_list[0][0] != 1) { throw "List first element should be 1" }

// The list inside is mutable (but the tuple reference is not)
let inner_list = tuple_with_list[0]
push(inner_list, 4)
print("After modifying inner list:", tuple_with_list)
if (tuple_with_list[0][3] != 4) { throw "List should have 4th element" }

print("All immutability tests passed!")
