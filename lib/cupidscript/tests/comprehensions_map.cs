// Map comprehension tests

print("=== Basic Map Comprehensions ===")

// Simple map transformation
let data = {a: 1, b: 2, c: 3}
let doubled = {k: v * 2 for k, v in data}
print("Doubled:", len(doubled))
if (doubled["a"] != 2) { throw "doubled['a'] should be 2" }
if (doubled["b"] != 4) { throw "doubled['b'] should be 4" }
if (doubled["c"] != 6) { throw "doubled['c'] should be 6" }

// Key transformation (manual since comprehensions only support simple keys)
let upper_keys = {}
for pair in items(data) {
    let k = pair[0]
    let v = pair[1]
    upper_keys[upper(k)] = v
}
print("Upper keys:", len(upper_keys))
if (upper_keys["A"] != 1) { throw "upper_keys['A'] should be 1" }
if (upper_keys["B"] != 2) { throw "upper_keys['B'] should be 2" }
if (upper_keys["C"] != 3) { throw "upper_keys['C'] should be 3" }

// Both key and value transformation (manual since comprehensions only support simple keys)
let transformed = {}
for pair in items(data) {
    let k = pair[0]
    let v = pair[1]
    transformed[k + "_new"] = v + 10
}
print("Transformed:", len(transformed))
if (transformed["a_new"] != 11) { throw "transformed['a_new'] should be 11" }
if (transformed["c_new"] != 13) { throw "transformed['c_new'] should be 13" }

print("\n=== Filtered Map Comprehensions ===")

// Filter by value
let high_values = {k: v for k, v in data if v > 1}
print("High values:", len(high_values))
if (len(high_values) != 2) { throw "high_values should have 2 entries" }
if (high_values["b"] != 2) { throw "high_values['b'] should be 2" }
if (high_values["c"] != 3) { throw "high_values['c'] should be 3" }

// Filter by key
let selected_keys = {k: v for k, v in data if k == "a" || k == "c"}
print("Selected keys:", len(selected_keys))
if (len(selected_keys) != 2) { throw "selected_keys should have 2 entries" }
if (selected_keys["a"] != 1) { throw "selected_keys['a'] should be 1" }
if (selected_keys["c"] != 3) { throw "selected_keys['c'] should be 3" }

print("\n=== From List to Map ===")

// List indices as keys
let numbers = [10, 20, 30, 40]
let indexed = {v: i for i, v in numbers}
print("Indexed:", len(indexed))
if (indexed[0] != 10) { throw "indexed[0] should be 10" }
if (indexed[3] != 40) { throw "indexed[3] should be 40" }

// String indices
let words = ["apple", "banana", "cherry"]
let word_map = {w: i for i, w in words}
print("Word map:", len(word_map))
if (word_map[0] != "apple") { throw "word_map[0] should be apple" }
if (word_map[2] != "cherry") { throw "word_map[2] should be cherry" }

// Custom key from value
let word_lengths = {i: len(i) for i, w in words}
print("Word lengths:", len(word_lengths))
if (word_lengths["apple"] != 5) { throw "word_lengths['apple'] should be 5" }
if (word_lengths["banana"] != 6) { throw "word_lengths['banana'] should be 6" }
if (word_lengths["cherry"] != 6) { throw "word_lengths['cherry'] should be 6" }

print("\n=== Complex Transformations ===")

// Numeric calculations
let scores = {alice: 85, bob: 92, charlie: 78, diana: 95}
let letter_grades = {k: v >= 90 ? "A" : v >= 80 ? "B" : "C" for k, v in scores}
print("Letter grades:", len(letter_grades))
if (letter_grades["alice"] != "B") { throw "letter_grades['alice'] should be B" }
if (letter_grades["bob"] != "A") { throw "letter_grades['bob'] should be A" }
if (letter_grades["charlie"] != "C") { throw "letter_grades['charlie'] should be C" }

// String operations
let messages = {user1: "hello", user2: "world", user3: "cupid"}
let capitalized = {k: upper(v) for k, v in messages}
print("Capitalized:", len(capitalized))
if (capitalized["user1"] != "HELLO") { throw "capitalized['user1'] should be HELLO" }
if (capitalized["user3"] != "CUPID") { throw "capitalized['user3'] should be CUPID" }

print("\n=== Edge Cases ===")

// Empty map
let empty_map = {k: v for k, v in {}}
print("Empty map:", len(empty_map))
if (len(empty_map) != 0) { throw "empty_map should be empty" }

// Filter that excludes everything
let none = {k: v for k, v in data if v > 100}
print("None:", len(none))
if (len(none) != 0) { throw "none should be empty" }

// Single entry
let single = {k: v * 10 for k, v in {x: 5}}
print("Single:", len(single))
if (len(single) != 1) { throw "single should have 1 entry" }
if (single["x"] != 50) { throw "single['x'] should be 50" }

// Swap keys and values
let original = {one: 1, two: 2, three: 3}
let swapped = {v: k for k, v in original}
print("Swapped:", len(swapped))
if (swapped[1] != "one") { throw "swapped[1] should be 'one'" }
if (swapped[2] != "two") { throw "swapped[2] should be 'two'" }
if (swapped[3] != "three") { throw "swapped[3] should be 'three'" }

print("\nAll map comprehension tests passed!")
