// Comprehensive tests for multiple iterator support in comprehensions

// Helper function to check list equality
fn lists_equal(a, b) {
    if (len(a) != len(b)) { return false }
    for i in range(len(a)) {
        if (a[i] != b[i]) { return false }
    }
    return true
}

// ============ BASIC MULTIPLE ITERATOR TESTS ============

// Test 1: Two iterators - flatten a 2D list
let matrix = [[1, 2], [3, 4], [5, 6]]
let flat = [item for row in matrix for item in row]
assert(len(flat) == 6, "Two iterator flatten length")
assert(lists_equal(flat, [1, 2, 3, 4, 5, 6]), "Two iterator flatten values")

// Test 2: Two iterators with ranges - Cartesian product
let pairs = [x * 10 + y for x in range(3) for y in range(3)]
assert(len(pairs) == 9, "Two range iterators count")
assert(pairs[0] == 0, "Cartesian [0,0]")
assert(pairs[4] == 11, "Cartesian [1,1]")
assert(pairs[8] == 22, "Cartesian [2,2]")

// Test 3: Three iterators
let triple = [x * 100 + y * 10 + z for x in range(2) for y in range(2) for z in range(2)]
assert(len(triple) == 8, "Three iterators count")
assert(triple[0] == 0, "Triple [0,0,0]")
assert(triple[7] == 111, "Triple [1,1,1]")

// Test 4: Four iterators (stress test)
let quad = [1 for a in range(2) for b in range(2) for c in range(2) for d in range(2)]
assert(len(quad) == 16, "Four iterators count")

// ============ WITH FILTERS ============

// Test 5: Two iterators with filter
let filtered = [(x, y) for x in range(5) for y in range(5) if x < y]
assert(len(filtered) == 10, "Two iterators with filter count")
// Pairs should be: (0,1), (0,2), (0,3), (0,4), (1,2), (1,3), (1,4), (2,3), (2,4), (3,4)

// Test 6: Three iterators with complex filter
let complex_filter = [x + y + z for x in range(3) for y in range(3) for z in range(3) if x + y + z > 4]
assert(len(complex_filter) == 4, "Three iterators complex filter")  // (1,2,2), (2,1,2), (2,2,1), (2,2,2)

// Test 7: Filter referencing multiple loop variables
let sum_filter = [x * y for x in range(1, 6) for y in range(1, 6) if x + y == 7]
assert(len(sum_filter) == 4, "Filter with sum condition")
// Should be: (2,5), (3,4), (4,3), (5,2)

// ============ DIFFERENT ITERABLE TYPES ============

// Test 8: List and range
let list_range = [c for item in [10, 20] for c in range(item, item + 2)]
assert(len(list_range) == 4, "List and range count")
assert(lists_equal(list_range, [10, 11, 20, 21]), "List and range values")

// Test 9: String and list
let str_list = [c for word in ["hi", "go"] for c in word]
assert(len(str_list) == 4, "String and list count")
assert(lists_equal(str_list, ["h", "i", "g", "o"]), "String and list values")

// Test 10: String and range
let str_range = [c for c in "AB" for n in range(2)]
assert(len(str_range) == 4, "String and range count")
assert(lists_equal(str_range, ["A", "A", "B", "B"]), "String and range values")

// Test 11: Map and list
let map_list = [k for m in [{a: 1}, {b: 2}] for k in m]
assert(len(map_list) == 2, "Map and list count")

// ============ DEPENDENT ITERABLES ============

// Test 12: Second iterable depends on first variable
let jagged = [[1, 2, 3], [4, 5], [6]]
let jagged_flat = [item for row in jagged for item in row]
assert(len(jagged_flat) == 6, "Jagged array flatten count")
assert(lists_equal(jagged_flat, [1, 2, 3, 4, 5, 6]), "Jagged array flatten values")

// Test 13: Third iterable depends on second variable
let nested = [[[1, 2], [3]], [[4]]]
let deep_flat = [item for level1 in nested for level2 in level1 for item in level2]
assert(len(deep_flat) == 4, "Deep nested flatten count")
assert(lists_equal(deep_flat, [1, 2, 3, 4]), "Deep nested flatten values")

// Test 14: Using range based on previous variable
let var_ranges = [j for i in [2, 3, 1] for j in range(i)]
assert(len(var_ranges) == 6, "Variable ranges count")
assert(lists_equal(var_ranges, [0, 1, 0, 1, 2, 0]), "Variable ranges values")

// ============ COMPLEX EXPRESSIONS ============

// Test 15: Complex expression in comprehension
let complex_expr = [(x + y, x * y) for x in range(3) for y in range(3) if x != y]
assert(len(complex_expr) == 6, "Complex expression count")

// Test 16: Nested function calls
fn double(x) { return x * 2 }
let with_func = [double(x + y) for x in range(2) for y in range(2)]
assert(len(with_func) == 4, "With function count")
assert(lists_equal(with_func, [0, 2, 2, 4]), "With function values")

// Test 17: String operations
let strings = [upper(c) for word in ["ab", "cd"] for c in word]
assert(len(strings) == 4, "String operations count")
assert(lists_equal(strings, ["A", "B", "C", "D"]), "String operations values")

// ============ MAP COMPREHENSIONS ============

// Test 18: Two iterators in map comprehension
let map2 = {x * 10 + y: x + y for x in range(3) for y in range(3)}
assert(len(map2) == 9, "Map with two iterators count")
assert(map2[0] == 0, "Map [0,0]")
assert(map2[11] == 2, "Map [1,1]")
assert(map2[22] == 4, "Map [2,2]")

// Test 19: Three iterators in map comprehension
let map3 = {x * 100 + y * 10 + z: x + y + z for x in range(2) for y in range(2) for z in range(2)}
assert(len(map3) == 8, "Map with three iterators count")
assert(map3[0] == 0, "Map [0,0,0]")
assert(map3[111] == 3, "Map [1,1,1]")

// Test 20: Map comprehension with filter
let map_filtered = {x: y for x in range(5) for y in range(5) if x == y}
assert(len(map_filtered) == 5, "Map with filter count")
assert(map_filtered[0] == 0, "Map filtered [0]")
assert(map_filtered[4] == 4, "Map filtered [4]")

// Test 21: Map comprehension with dependent iterables
let lists = [[1, 2], [3, 4]]
let map_deps = {item: item * 2 for lst in lists for item in lst}
assert(len(map_deps) == 4, "Map dependent iterables")  // Keys: 1, 2, 3, 4

// ============ EDGE CASES ============

// Test 22: Empty iterables
let empty1 = [x for x in [] for y in range(3)]
assert(len(empty1) == 0, "First iterable empty")

let empty2 = [x for x in range(3) for y in []]
assert(len(empty2) == 0, "Second iterable empty")

// Test 23: Single element iterables
let single = [x * y for x in [5] for y in [7]]
assert(len(single) == 1, "Single element iterables count")
assert(single[0] == 35, "Single element iterables value")

// Test 24: Many iterations with filter (performance)
let many = [x for x in range(10) for y in range(10) if x == y]
assert(len(many) == 10, "Many iterations with filter")

// ============ ITERATION WITH INDEX VARIABLES ============

// Test 25: Using second variable (index) from first iterable
let with_idx = [i for v, i in [10, 20, 30] for j in range(2)]
assert(len(with_idx) == 6, "With index count")
assert(lists_equal(with_idx, [0, 0, 1, 1, 2, 2]), "With index values")

// Test 26: Map iteration with value variable
let map_vals = [v for k, v in {a: 1, b: 2} for i in range(2)]
assert(len(map_vals) == 4, "Map values count")

print("All multiple iterator comprehension tests passed!")
