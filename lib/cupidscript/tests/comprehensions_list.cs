// List comprehension tests

print("=== Basic List Comprehensions ===")

// Simple expression
let squares = [x * x for x in range(10)]
print("Squares:", len(squares))
if (len(squares) != 10) { throw "squares should have 10 elements" }
if (squares[0] != 0) { throw "squares[0] should be 0" }
if (squares[9] != 81) { throw "squares[9] should be 81" }

// With filter
let evens = [x for x in range(20) if x % 2 == 0]
print("Evens:", len(evens))
if (len(evens) != 10) { throw "evens should have 10 elements" }
if (evens[0] != 0) { throw "evens[0] should be 0" }
if (evens[9] != 18) { throw "evens[9] should be 18" }

// From list
let numbers = [1, 2, 3, 4, 5]
let doubled = [n * 2 for n in numbers]
print("Doubled:", len(doubled))
if (len(doubled) != 5) { throw "doubled should have 5 elements" }
if (doubled[0] != 2) { throw "doubled[0] should be 2" }
if (doubled[4] != 10) { throw "doubled[4] should be 10" }

// Filter on list
let odds = [n for n in numbers if n % 2 == 1]
print("Odds:", len(odds))
if (len(odds) != 3) { throw "odds should have 3 elements" }
if (odds[0] != 1) { throw "odds[0] should be 1" }
if (odds[2] != 5) { throw "odds[2] should be 5" }

print("\n=== String Transformations ===")

let words = ["hello", "world", "cupid"]
let uppercase = [upper(w) for w in words]
print("Uppercase:", len(uppercase))
if (uppercase[0] != "HELLO") { throw "uppercase[0] should be HELLO" }
if (uppercase[2] != "CUPID") { throw "uppercase[2] should be CUPID" }

let lengths = [len(w) for w in words]
print("Lengths:", len(lengths))
if (lengths[0] != 5) { throw "lengths[0] should be 5" }
if (lengths[1] != 5) { throw "lengths[1] should be 5" }

// Filter strings
let long_words = [w for w in words if len(w) > 4]
print("Long words:", len(long_words))
if (len(long_words) != 3) { throw "long_words should have 3 elements" }

print("\n=== Map Iteration ===")

let map_data = {a: 1, b: 2, c: 3}
let keys = [k for k in map_data]
print("Keys:", len(keys))
if (len(keys) != 3) { throw "keys should have 3 elements" }

let key_vals = [k + ":" + to_str(v) for k, v in map_data]
print("Key-value pairs:", len(key_vals))
if (len(key_vals) != 3) { throw "key_vals should have 3 elements" }

// Filter on map values
let high_values = [k for k, v in map_data if v > 1]
print("High value keys:", len(high_values))
if (len(high_values) != 2) { throw "high_values should have 2 elements" }

print("\n=== Complex Expressions ===")

// Multiple operations
let complex = [x * 2 + 1 for x in range(5)]
print("Complex:", len(complex))
if (complex[0] != 1) { throw "complex[0] should be 1" }
if (complex[4] != 9) { throw "complex[4] should be 9" }

// Function calls in expression
let data = [1, 2, 3, 4, 5]
let stringified = [to_str(x) for x in data]
print("Stringified:", len(stringified))
if (typeof(stringified[0]) != "string") { throw "stringified[0] should be string" }

// Conditional expression
let signs = [x >= 0 ? x : -x for x in [-2, -1, 0, 1, 2]]
print("Signs:", len(signs))
if (signs[0] != 2) { throw "signs[0] should be 2" }
if (signs[4] != 2) { throw "signs[4] should be 2" }

print("\n=== Edge Cases ===")

// Empty range
let empty = [x for x in range(0)]
print("Empty:", len(empty))
if (len(empty) != 0) { throw "empty should have 0 elements" }

// Empty list
let empty_list = [x * 2 for x in []]
print("Empty list:", len(empty_list))
if (len(empty_list) != 0) { throw "empty_list should have 0 elements" }

// Filter that excludes everything
let none = [x for x in range(10) if x > 100]
print("None:", len(none))
if (len(none) != 0) { throw "none should have 0 elements" }

// Single element
let single = [x * x for x in [5]]
print("Single:", len(single))
if (len(single) != 1) { throw "single should have 1 element" }
if (single[0] != 25) { throw "single[0] should be 25" }

print("\nAll list comprehension tests passed!")
