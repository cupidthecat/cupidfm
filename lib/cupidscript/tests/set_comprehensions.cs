// Set Comprehension Tests

print("=== Set Comprehension Tests ===\n");

// Test 1: Basic set comprehension from list
print("Test 1: Basic set from list");
let nums = [1, 2, 3, 2, 1, 4, 3];
let unique = {x for x in nums};
assert(typeof(unique) == "set", "Type should be set");
assert(len(unique) == 4, "Should have 4 unique elements");
assert(set_has(unique, 1) && set_has(unique, 2) && set_has(unique, 3) && set_has(unique, 4), "Should contain 1,2,3,4");

// Test 2: Set comprehension with transformation
print("Test 2: Set with transformation");
let lengths = {len(word) for word in ["hi", "hello", "hey", "h"]};
assert(typeof(lengths) == "set", "Type should be set");
assert(len(lengths) == 4, "Should have 4 unique lengths");
assert(set_has(lengths, 1) && set_has(lengths, 2) && set_has(lengths, 3) && set_has(lengths, 5), "Should contain lengths 1,2,3,5");

// Test 3: Set comprehension with filter
print("Test 3: Set with filter");
let evens = {x for x in range(10) if x % 2 == 0};
assert(len(evens) == 5, "Should have 5 even numbers");
assert(set_has(evens, 0) && set_has(evens, 2) && set_has(evens, 4) && set_has(evens, 6) && set_has(evens, 8), "Should contain 0,2,4,6,8");

// Test 4: Set comprehension from range
print("Test 4: Set from range");
let range_set = {x * 2 for x in range(5)};
assert(len(range_set) == 5, "Should have 5 elements");
assert(set_has(range_set, 0) && set_has(range_set, 2) && set_has(range_set, 4) && set_has(range_set, 6) && set_has(range_set, 8), "Should contain 0,2,4,6,8");

// Test 5: Set comprehension from map (keys)
print("Test 5: Set from map keys");
let data = {a: 1, b: 2, c: 3};
let keys_set = {k for k, v in data};
assert(len(keys_set) == 3, "Should have 3 keys");
assert(set_has(keys_set, "a") && set_has(keys_set, "b") && set_has(keys_set, "c"), "Should contain a,b,c");

// Test 6: Set comprehension from map (values)
print("Test 6: Set from map values");
let values_set = {v for k, v in data};
assert(len(values_set) == 3, "Should have 3 values");
assert(set_has(values_set, 1) && set_has(values_set, 2) && set_has(values_set, 3), "Should contain 1,2,3");

// Test 7: Set comprehension with modulo (duplicates removed)
print("Test 7: Set with modulo");
let modulo_set = {x % 3 for x in range(10)};
assert(len(modulo_set) == 3, "Should have 3 unique values");
assert(set_has(modulo_set, 0) && set_has(modulo_set, 1) && set_has(modulo_set, 2), "Should contain 0,1,2");

// Test 8: Set comprehension from string
print("Test 8: Set from string characters");
let chars = {c for c in "hello"};
assert(typeof(chars) == "set", "Type should be set");
assert(len(chars) == 4, "Should have 4 unique chars");
assert(set_has(chars, "h") && set_has(chars, "e") && set_has(chars, "l") && set_has(chars, "o"), "Should contain h,e,l,o");

// Test 9: Empty set comprehension
print("Test 9: Empty set");
let empty_set = {x for x in [] if x > 0};
assert(typeof(empty_set) == "set", "Type should be set");
assert(len(empty_set) == 0, "Should be empty");

// Test 10: Set comprehension with boolean expressions
print("Test 10: Set with boolean");
let bool_set = {x > 5 for x in range(10)};
assert(len(bool_set) == 2, "Should have 2 unique boolean values");
assert(set_has(bool_set, true) && set_has(bool_set, false), "Should contain true and false");

// Test 11: Set comprehension with nested structure
print("Test 11: Set from nested lists");
let matrix = [[1, 2], [3, 4], [1, 2]];
let first_elements = {row[0] for row in matrix};
assert(len(first_elements) == 2, "Should have 2 unique first elements");
assert(set_has(first_elements, 1) && set_has(first_elements, 3), "Should contain 1 and 3");

// Test 12: Set comprehension with string operations
print("Test 12: Set with string ops");
let words = ["apple", "APPLE", "Apple", "banana", "BANANA"];
let lower_words = {lower(w) for w in words};
assert(len(lower_words) == 2, "Should have 2 unique lowercase words");
assert(set_has(lower_words, "apple") && set_has(lower_words, "banana"), "Should contain apple and banana");

// Test 13: Set comprehension with arithmetic
print("Test 13: Set with arithmetic");
let squares = {x * x for x in range(5)};
assert(len(squares) == 5, "Should have 5 squares");
assert(set_has(squares, 0) && set_has(squares, 1) && set_has(squares, 4) && set_has(squares, 9) && set_has(squares, 16), "Should contain 0,1,4,9,16");

// Test 14: Set comprehension complex filter
print("Test 14: Set with complex filter");
let filtered = {x for x in range(20) if x % 2 == 0 && x % 3 == 0};
assert(len(filtered) == 4, "Should have 4 elements divisible by both 2 and 3");
assert(set_has(filtered, 0) && set_has(filtered, 6) && set_has(filtered, 12) && set_has(filtered, 18), "Should contain 0,6,12,18");

// Test 15: Set from set (idempotent)
print("Test 15: Set from set");
let original_set = set([1, 2, 3, 2, 1]);
let new_set = {x for x in original_set};
assert(typeof(new_set) == "set", "Type should be set");
assert(len(new_set) == 3, "Should preserve uniqueness");

print("\n✅ All set comprehension tests passed!");
