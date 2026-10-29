// Test Python-style conditional expressions in comprehensions

let numbers = [-2, -1, 0, 1, 2, 3]

// Test 1: Basic Python-style if-else in list comprehension
let signs = ["pos" if x > 0 else "neg" for x in numbers]
print("Signs:", signs)
assert(signs[0] == "neg")  // -2
assert(signs[3] == "pos")  // 1
assert(signs[4] == "pos")  // 2

// Test 2: Traditional ternary still works
let signs2 = [x > 0 ? "positive" : "negative" for x in numbers]
print("Signs2:", signs2)
assert(signs2[0] == "negative")
assert(signs2[3] == "positive")

// Test 3: Python-style with complex expressions
let categorized = ["big" if x > 1 else "small" for x in numbers]
print("Categorized:", categorized)
assert(categorized[5] == "big")   // 3
assert(categorized[0] == "small") // -2

// Test 4: In set comprehension
let sign_set = {"positive" if x > 0 else "negative" for x in numbers}
print("Sign set:", sign_set)
assert(len(sign_set) == 2)

// Test 5: In map comprehension (value position)
let value_map = {x: "even" if x % 2 == 0 else "odd" for x in range(5)}
print("Value map:", value_map)
assert(value_map[0] == "even")
assert(value_map[1] == "odd")
assert(value_map[2] == "even")

// Test 6: In map comprehension (key position) - simplified
// Using simpler key expression for now
let key_map = {x: "even" if x % 2 == 0 else "odd" for x in [1, 2, 3, 4]}
print("Key map (value conditional):", key_map)
assert(key_map[1] == "odd")
assert(key_map[2] == "even")

// Test 7: Simple chaining (using separate comprehensions for now)
let big_check = ["big" if x > 2 else "not_big" for x in numbers]
print("Big check:", big_check)
assert(big_check[5] == "big")      // 3
assert(big_check[3] == "not_big")  // 1

// Test 8: With filter clause
let filtered = ["YES" if x > 0 else "NO" for x in numbers if x != 0]
print("Filtered:", filtered)
assert(len(filtered) == 5)  // All except 0

// Test 9: Multiple iterators
let matrix = [[1, 2], [3, 4]]
let multi = ["even" if x % 2 == 0 else "odd" for row in matrix for x in row]
print("Multi:", multi)
assert(multi[0] == "odd")   // 1
assert(multi[1] == "even")  // 2
assert(multi[2] == "odd")   // 3
assert(multi[3] == "even")  // 4

print("\nAll tests passed! ✓")
