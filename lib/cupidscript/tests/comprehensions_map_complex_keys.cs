// Test map comprehensions with complex key expressions
// This feature allows arbitrary expressions as keys, not just simple identifiers

print("=== Function Calls as Keys ===")

let data = {a: 1, b: 2, c: 3}

// Upper case keys
let upper_keys = {upper(k): v for k, v in data}
print("Upper keys:", len(upper_keys))
if (upper_keys["A"] != 1) { throw "upper_keys['A'] should be 1" }
if (upper_keys["B"] != 2) { throw "upper_keys['B'] should be 2" }
if (upper_keys["C"] != 3) { throw "upper_keys['C'] should be 3" }

// Lower case keys
let mixed = {X: 1, Y: 2, Z: 3}
let lower_keys = {lower(k): v for k, v in mixed}
print("Lower keys:", len(lower_keys))
if (lower_keys["x"] != 1) { throw "lower_keys['x'] should be 1" }
if (lower_keys["z"] != 3) { throw "lower_keys['z'] should be 3" }

print("\n=== String Expressions as Keys ===")

// Concatenation
let prefixed = {k + "_key": v for k, v in data}
print("Prefixed:", len(prefixed))
if (prefixed["a_key"] != 1) { throw "prefixed['a_key'] should be 1" }
if (prefixed["c_key"] != 3) { throw "prefixed['c_key'] should be 3" }

// Multiple operations
let transformed = {upper(k) + "_NEW": v * 10 for k, v in data}
print("Transformed:", len(transformed))
if (transformed["A_NEW"] != 10) { throw "transformed['A_NEW'] should be 10" }
if (transformed["B_NEW"] != 20) { throw "transformed['B_NEW'] should be 20" }
if (transformed["C_NEW"] != 30) { throw "transformed['C_NEW'] should be 30" }

print("\n=== Numeric Expressions as Keys ===")

let numbers = {a: 1, b: 2, c: 3, d: 4}

// Multiply key values
let swapped_scaled = {v * 10: k for k, v in numbers}
print("Swapped scaled:", len(swapped_scaled))
if (swapped_scaled[10] != "a") { throw "swapped_scaled[10] should be 'a'" }
if (swapped_scaled[20] != "b") { throw "swapped_scaled[20] should be 'b'" }
if (swapped_scaled[40] != "d") { throw "swapped_scaled[40] should be 'd'" }

// Computed keys
let computed = {v * v: k for k, v in numbers}
print("Computed:", len(computed))
if (computed[1] != "a") { throw "computed[1] should be 'a'" }
if (computed[4] != "b") { throw "computed[4] should be 'b'" }
if (computed[9] != "c") { throw "computed[9] should be 'c'" }
if (computed[16] != "d") { throw "computed[16] should be 'd'" }

print("\n=== Complex Expressions ===")

// Key from both variables
let combined = {k + "_" + to_str(v): v * v for k, v in data}
print("Combined:", len(combined))
if (combined["a_1"] != 1) { throw "combined['a_1'] should be 1" }
if (combined["b_2"] != 4) { throw "combined['b_2'] should be 4" }
if (combined["c_3"] != 9) { throw "combined['c_3'] should be 9" }

// Value-based keys
let val_keys = {to_str(v * 100): k for k, v in data}
print("Val keys:", len(val_keys))
if (val_keys["100"] != "a") { throw "val_keys['100'] should be 'a'" }
if (val_keys["200"] != "b") { throw "val_keys['200'] should be 'b'" }
if (val_keys["300"] != "c") { throw "val_keys['300'] should be 'c'" }

print("\n=== With Filters ===")

// Complex keys with filtering
let filtered = {k + "_big": v for k, v in numbers if v > 2}
print("Filtered:", len(filtered))
if (len(filtered) != 2) { throw "filtered should have 2 entries" }
if (filtered["c_big"] != 3) { throw "filtered['c_big'] should be 3" }
if (filtered["d_big"] != 4) { throw "filtered['d_big'] should be 4" }

// Function call keys with filter
let upper_filtered = {upper(k): v for k, v in data if v % 2 == 0}
print("Upper filtered:", len(upper_filtered))
if (len(upper_filtered) != 1) { throw "upper_filtered should have 1 entry" }
if (upper_filtered["B"] != 2) { throw "upper_filtered['B'] should be 2" }

print("\n=== From Lists ===")

let fruits = ["apple", "banana", "cherry", "date"]

// Length as key
let by_length = {len(fruit): fruit for fruit, idx in fruits}
print("By length:", len(by_length))
// Note: banana (6) gets overwritten by cherry (6)
if (by_length[5] != "apple") { throw "by_length[5] should be 'apple'" }
if (by_length[6] != "cherry") { throw "by_length[6] should be 'cherry'" }
if (by_length[4] != "date") { throw "by_length[4] should be 'date'" }

// First char as key
let by_first = {substr(fruit, 0, 1): fruit for fruit, idx in fruits}
print("By first char:", len(by_first))
if (by_first["a"] != "apple") { throw "by_first['a'] should be 'apple'" }
if (by_first["b"] != "banana") { throw "by_first['b'] should be 'banana'" }
if (by_first["c"] != "cherry") { throw "by_first['c'] should be 'cherry'" }
if (by_first["d"] != "date") { throw "by_first['d'] should be 'date'" }

print("\n=== Ternary in Keys ===")

// Conditional key generation
let scored = {a: 85, b: 92, c: 78, d: 95}
let graded = {v >= 90 ? "A_" + k : "B_" + k: v for k, v in scored}
print("Graded:", len(graded))
if (graded["B_a"] != 85) { throw "graded['B_a'] should be 85" }
if (graded["A_b"] != 92) { throw "graded['A_b'] should be 92" }
if (graded["B_c"] != 78) { throw "graded['B_c'] should be 78" }
if (graded["A_d"] != 95) { throw "graded['A_d'] should be 95" }

print("\n=== Empty and Edge Cases ===")

// Empty map with complex keys
let empty_result = {upper(k): v for k, v in {}}
print("Empty result:", len(empty_result))
if (len(empty_result) != 0) { throw "empty_result should be empty" }

// Single entry
let single = {k + "!": v * 100 for k, v in {x: 5}}
print("Single:", len(single))
if (len(single) != 1) { throw "single should have 1 entry" }
if (single["x!"] != 500) { throw "single['x!'] should be 500" }

// Key collision (one of the values wins, order not guaranteed)
let collisions = {a: 1, b: 2, c: 3}
let same_key = {"KEY": v for k, v in collisions}
print("Same key:", len(same_key))
if (len(same_key) != 1) { throw "same_key should have 1 entry (collision)" }
// Value will be one of 1, 2, or 3 (map iteration order not guaranteed)
let val = same_key["KEY"]
if (val != 1 && val != 2 && val != 3) { throw "same_key['KEY'] should be 1, 2, or 3" }

print("\nAll complex key comprehension tests passed!")
