// Multiple Iterator Comprehensions - Examples and Use Cases

print("============================================================")
print("MULTIPLE ITERATOR COMPREHENSIONS")
print("============================================================")
print("")

// ============================================================
// 1. FLATTENING NESTED LISTS
// ============================================================

print("1. Flattening Nested Lists")
print("----------------------------------------")

// Flatten a 2D matrix
let matrix = [
    [1, 2, 3],
    [4, 5, 6],
    [7, 8, 9]
]

let flat = [item for row in matrix for item in row]
print("Matrix:", matrix)
print("Flattened:", flat)
print("")

// Flatten jagged arrays
let jagged = [[1, 2], [3], [4, 5, 6]]
let jagged_flat = [x for row in jagged for x in row]
print("Jagged array:", jagged)
print("Flattened:", jagged_flat)
print("")

// ============================================================
// 2. CARTESIAN PRODUCTS
// ============================================================

print("2. Cartesian Products")
print("----------------------------------------")

// Generate all coordinate pairs
let coords = [(x, y) for x in range(3) for y in range(3)]
print("All coordinate pairs (0-2):")
for coord in coords {
    print("  ", coord)
}
print("")

// Generate all combinations of two sets
let colors = ["red", "blue"]
let sizes = ["small", "medium", "large"]
let products = [(color, size) for color in colors for size in sizes]
print("Product combinations:")
for product in products {
    print(fmt("  %v", product))
}
print("")

// ============================================================
// 3. MATHEMATICAL OPERATIONS
// ============================================================

print("3. Mathematical Operations")
print("----------------------------------------")

// Multiplication table
let mult_table = [(x, y, x * y) for x in range(1, 6) for y in range(1, 6)]
print("Multiplication table (1-5):")
print("First 10 entries:", mult_table |> slice(0, 10))
print("")

// Pythagorean triples (a² + b² = c²)
fn is_pythagorean(a, b, c) {
    return a * a + b * b == c * c
}

let triples = [(a, b, c)
    for a in range(1, 15)
    for b in range(a, 15)
    for c in range(b, 20)
    if is_pythagorean(a, b, c)]

print("Pythagorean triples (a ≤ b < c < 20):")
for triple in triples {
    print("  ", triple)
}
print("")

// ============================================================
// 4. STRING PROCESSING
// ============================================================

print("4. String Processing")
print("----------------------------------------")

// Generate all letter-digit combinations
let combos = [fmt("%s%s", letter, digit)
    for letter in "ABC"
    for digit in "123"]
print("Letter-digit combinations:", combos)
print("")

// Extract all characters from words
let words = ["hello", "world"]
let chars = [c for word in words for c in word]
print("Words:", words)
print("All characters:", chars)
print("")

// ============================================================
// 5. DATA TRANSFORMATION
// ============================================================

print("5. Data Transformation")
print("----------------------------------------")

// Transform nested data structures
let data = [
    {name: "Alice", scores: [85, 90, 95]},
    {name: "Bob", scores: [75, 80, 85]}
]

let all_scores = [score for person in data for score in person["scores"]]
print("All scores:", all_scores)

fn sum_list(lst) {
    let total = 0
    for item in lst {
        total = total + item
    }
    return total
}

let avg = sum_list(all_scores) / len(all_scores)
print("Average score:", avg)
print("")

// ============================================================
// 6. FILTERING WITH MULTIPLE CONDITIONS
// ============================================================

print("6. Filtering with Multiple Conditions")
print("----------------------------------------")

// Find pairs where x < y
let pairs = [(x, y) for x in range(5) for y in range(5) if x < y]
print("Pairs where x < y:", pairs)
print("")

// Find numbers divisible by both i and j
let divisible = [
    (i, j, i * j)
    for i in range(2, 6)
    for j in range(2, 6)
    if i != j
]
print("Products where i ≠ j:")
for item in divisible {
    print("  ", item)
}
print("")

// ============================================================
// 7. MAP COMPREHENSIONS WITH MULTIPLE ITERATORS
// ============================================================

print("7. Map Comprehensions with Multiple Iterators")
print("----------------------------------------")

// Create a lookup table
let lookup = {x * 10 + y: x + y for x in range(3) for y in range(3)}
print("Lookup table (key = x*10+y, value = x+y):")
for k in lookup {
    print(fmt("  %d: %d", k, lookup[k]))
}
print("")

// Create a simple mapping
let keys = [1, 2, 3]
let values = [10, 20, 30]
let simple_map = {k: v for k in keys for v in values if k == v / 10}
print("Simple mapping (k = v/10):")
for k in simple_map {
    print(fmt("  %d: %d", k, simple_map[k]))
}
print("")

// ============================================================
// 8. PRACTICAL EXAMPLES
// ============================================================

print("8. Practical Examples")
print("----------------------------------------")

// Generate test data for a grid
fn cell_value(row, col) {
    return row * 10 + col
}

let grid_values = [cell_value(r, c) for r in range(3) for c in range(3)]
print("Grid values (3x3):", grid_values)
print("")

// Process multi-level configuration
let environments = ["dev", "staging"]
let services = ["api", "db", "cache"]
let configs = [fmt("%s-%s", env, svc)
    for env in environments
    for svc in services]
print("All service configurations:", configs)
print("")

// ============================================================
// 9. DEEP NESTING (3+ ITERATORS)
// ============================================================

print("9. Deep Nesting (3+ Iterators)")
print("----------------------------------------")

// 3D coordinates
let cube = [(x, y, z)
    for x in range(2)
    for y in range(2)
    for z in range(2)]
print("3D cube coordinates (2x2x2):")
for coord in cube {
    print("  ", coord)
}
print("")

// Flatten a 3D structure
let structure_3d = [
    [[1, 2], [3, 4]],
    [[5, 6], [7, 8]]
]
let flat_3d = [item
    for level1 in structure_3d
    for level2 in level1
    for item in level2]
print("3D structure flattened:", flat_3d)
print("")

// ============================================================
// 10. PERFORMANCE PATTERNS
// ============================================================

print("10. Performance Patterns")
print("----------------------------------------")

// Efficient way to find all pairs without duplicates
let unique_pairs = [(i, j)
    for i in range(10)
    for j in range(i + 1, 10)]
print("Unique pairs (first 10):", unique_pairs |> slice(0, 10))
print("Total unique pairs:", len(unique_pairs))
print("")

// Generate sparse combinations
let sparse = [(x, y)
    for x in range(100)
    for y in range(100)
    if x % 10 == 0 && y % 10 == 0]
print("Sparse grid (every 10th):", len(sparse), "points")
print("")

print("============================================================")
print("All examples completed!")
print("============================================================")
