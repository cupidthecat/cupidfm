// Set Literals & Enhanced Set Operations Test Suite
// Tests for set literal syntax, set algebra operations, and set methods

// Test 1: Set Literals
println("Test 1: Set Literals");
let s1 = #{1, 2, 3};
println(s1);  // Should print #{1, 2, 3}

let s2 = #{3, 4, 5};
println(s2);  // Should print #{3, 4, 5}

let empty_set = #{};
println(empty_set);  // Should print #{}

// Test 2: Set with mixed types
println("\nTest 2: Mixed Type Sets");
let mixed = #{"hello", 42, true};
println(mixed);

// Test 3: Set Union (|)
println("\nTest 3: Set Union (|)");
let union = s1 | s2;
println("s1 | s2 =", union);  // Should be #{1, 2, 3, 4, 5}

// Verify union contains all elements
let expected_union = #{1, 2, 3, 4, 5};
println("Union has 5 elements:", union.size() == 5);

// Test 4: Set Intersection (&)
println("\nTest 4: Set Intersection (&)");
let intersection = s1 & s2;
println("s1 & s2 =", intersection);  // Should be #{3}

// Verify intersection
println("Intersection has 1 element:", intersection.size() == 1);
println("Intersection contains 3:", intersection.contains(3));

// Test 5: Set Difference (-)
println("\nTest 5: Set Difference (-)");
let difference = s1 - s2;
println("s1 - s2 =", difference);  // Should be #{1, 2}

// Verify difference
println("Difference has 2 elements:", difference.size() == 2);
println("Difference contains 1:", difference.contains(1));
println("Difference contains 2:", difference.contains(2));
println("Difference does not contain 3:", !difference.contains(3));

// Test 6: Set Symmetric Difference (^)
println("\nTest 6: Set Symmetric Difference (^)");
let symmetric = s1 ^ s2;
println("s1 ^ s2 =", symmetric);  // Should be #{1, 2, 4, 5}

// Verify symmetric difference
println("Symmetric diff has 4 elements:", symmetric.size() == 4);
println("Contains 1:", symmetric.contains(1));
println("Contains 2:", symmetric.contains(2));
println("Does not contain 3:", !symmetric.contains(3));
println("Contains 4:", symmetric.contains(4));
println("Contains 5:", symmetric.contains(5));

// Test 7: Set add method
println("\nTest 7: Set add method");
let s3 = #{1, 2, 3};
println("Before add:", s3);
let added = s3.add(6);
println("After adding 6:", s3);
println("add() returned true (new element):", added);

let added_again = s3.add(6);
println("add() returned false (existing element):", !added_again);

// Test 8: Set remove method
println("\nTest 8: Set remove method");
let s4 = #{1, 2, 3};
println("Before remove:", s4);
let removed = s4.remove(1);
println("After removing 1:", s4);
println("remove() returned true (element existed):", removed);

let removed_again = s4.remove(1);
println("remove() returned false (element doesn't exist):", !removed_again);

// Test 9: Set contains method
println("\nTest 9: Set contains method");
let s5 = #{1, 2, 3};
println("Contains 2:", s5.contains(2));
println("Contains 5:", s5.contains(5));

// Test 10: Set size method
println("\nTest 10: Set size method");
let s6 = #{1, 2, 3, 4, 5};
println("Set size:", s6.size());  // Should be 5

// Test 11: Set clear method
println("\nTest 11: Set clear method");
let s7 = #{1, 2, 3};
println("Before clear:", s7, "size:", s7.size());
s7.clear();
println("After clear:", s7, "size:", s7.size());  // Should be 0

// Test 12: Empty set operations
println("\nTest 12: Empty set operations");
let empty1 = #{};
let empty2 = #{};
let nonempty = #{1, 2};

println("empty | empty =", empty1 | empty2);
println("empty & empty =", empty1 & empty2);
println("empty - empty =", empty1 - empty2);
println("empty ^ empty =", empty1 ^ empty2);
println("nonempty | empty =", nonempty | empty1);
println("nonempty & empty =", nonempty & empty1);

// Test 13: Set with string elements
println("\nTest 13: String sets");
let fruits = #{"apple", "banana", "cherry"};
let berries = #{"strawberry", "blueberry", "cherry"};

println("All fruits:", fruits | berries);
println("Common fruits:", fruits & berries);
println("Only fruits:", fruits - berries);

// Test 14: Chain operations
println("\nTest 14: Chain operations");
let a = #{1, 2, 3};
let b = #{3, 4, 5};
let c = #{5, 6, 7};

// Complex expression: (a | b) & c
let result = (a | b) & c;
println("(a | b) & c =", result);  // Should be #{5}

// Test 15: Set with duplicate elements (should auto-deduplicate)
println("\nTest 15: Duplicate elements in literal");
let s8 = #{1, 2, 2, 3, 3, 3};
println("Set from #{1, 2, 2, 3, 3, 3}:", s8);
println("Size:", s8.size());  // Should be 3

// Test 16: Set spread operator
println("\nTest 16: Set spread operator");
let s9 = #{1, 2};
let s10 = #{...s9, 3, 4};
println("Set with spread:", s10);  // Should be #{1, 2, 3, 4}

// Test 17: Set comprehension
println("\nTest 17: Set comprehension");
let squares = #{x * x for x in [1, 2, 3, 4, 5]};
println("Squares comprehension:", squares);  // Should be #{1, 4, 9, 16, 25}

// Test 18: Set comprehension with filter
println("\nTest 18: Set comprehension with filter");
let even_squares = #{x * x for x in [1, 2, 3, 4, 5, 6] if x % 2 == 0};
println("Even squares:", even_squares);  // Should be #{4, 16, 36}

// Test 19: Set comprehension with multiple iterables
println("\nTest 19: Multi-iterator set comprehension");
let products = #{x * y for x in [1, 2, 3] for y in [1, 2, 3]};
println("Products:", products);  // Should be unique products

// Test 20: Set operations maintain set semantics
println("\nTest 20: Set semantics");
let original = #{1, 2, 3};
let modified = original;
modified.add(4);
println("Original after modifying alias:", original);  // Should show the modification (same reference)

println("\n✅ All set tests completed!");
