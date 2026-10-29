// Nested and advanced comprehension tests

print("=== Nested List Comprehensions ===")

// 2D matrix (outer comprehension creates inner comprehensions)
let matrix = [[i * j for j in range(5)] for i in range(5)]
print("Matrix (first row):", matrix[0])
print("Matrix (last row):", matrix[4])
if (len(matrix) != 5) { throw "matrix should have 5 rows" }
if (len(matrix[0]) != 5) { throw "matrix[0] should have 5 columns" }
if (matrix[2][3] != 6) { throw "matrix[2][3] should be 6 (2*3)" }
if (matrix[4][4] != 16) { throw "matrix[4][4] should be 16 (4*4)" }

// Flattening a list of lists (using flatMap-like pattern)
let nested = [[1, 2], [3, 4], [5, 6]]
// Manual flattening since double-for isn't supported
fn flatten(lists) {
    let result = []
    for sublist in lists {
        for item in sublist {
            push(result, item)
        }
    }
    return result
}
let flattened = flatten(nested)
print("Flattened:", flattened)
if (len(flattened) != 6) { throw "flattened should have 6 elements" }
if (flattened[0] != 1) { throw "flattened[0] should be 1" }
if (flattened[5] != 6) { throw "flattened[5] should be 6" }

// Coordinate pairs using nested function
fn make_coords(n) {
    let result = []
    for x in range(n) {
        for y in range(n) {
            push(result, (x, y))
        }
    }
    return result
}
let coords = make_coords(3)
print("Coords count:", len(coords))
if (len(coords) != 9) { throw "coords should have 9 elements" }
if (coords[0][0] != 0) { throw "coords[0][0] should be 0" }
if (coords[0][1] != 0) { throw "coords[0][1] should be 0" }
if (coords[8][0] != 2) { throw "coords[8][0] should be 2" }
if (coords[8][1] != 2) { throw "coords[8][1] should be 2" }

print("\n=== Comprehensions with Complex Filters ===")

// Multiple conditions using &&
let data = range(20)
let filtered = [x for x in data if (x % 2 == 0 && x % 3 == 0)]
print("Divisible by 2 and 3:", filtered)
if (len(filtered) != 4) { throw "filtered should have 4 elements (0,6,12,18)" }
if (filtered[1] != 6) { throw "filtered[1] should be 6" }
if (filtered[3] != 18) { throw "filtered[3] should be 18" }

// Complex boolean expression
let numbers = range(1, 21)
let special = [x for x in numbers if ((x > 5 && x < 15) || x == 20)]
print("Special numbers:", special)
if (len(special) != 10) { throw "special should have 10 elements" }

print("\n=== Comprehensions with Function Calls ===")

fn square(x) {
    return x * x
}

fn is_even(x) {
    return x % 2 == 0
}

let squared = [square(x) for x in range(6)]
print("Squared:", squared)
if (squared[5] != 25) { throw "squared[5] should be 25" }

let even_squares = [square(x) for x in range(10) if is_even(x)]
print("Even squares:", even_squares)
if (len(even_squares) != 5) { throw "even_squares should have 5 elements" }
if (even_squares[0] != 0) { throw "even_squares[0] should be 0" }
if (even_squares[4] != 64) { throw "even_squares[4] should be 64" }

print("\n=== Comprehensions with Variables ===")

let multiplier = 3
let scaled = [x * multiplier for x in range(5)]
print("Scaled:", scaled)
if (scaled[4] != 12) { throw "scaled[4] should be 12" }

let threshold = 50
let items = range(100)
let above_threshold = [x for x in items if x > threshold]
print("Above threshold count:", len(above_threshold))
if (len(above_threshold) != 49) { throw "above_threshold should have 49 elements" }

print("\n=== Map Comprehensions with Nesting ===")

// Create nested maps
let grid_map = {i: {j: i * 10 + j for j in range(3)} for i in range(3)}
print("Grid map 0:", grid_map[0])
print("Grid map 2:", grid_map[2])
if (grid_map[0][0] != 0) { throw "grid_map[0][0] should be 0" }
if (grid_map[2][2] != 22) { throw "grid_map[2][2] should be 22" }

// Transform nested data
let nested_data = {a: {x: 1, y: 2}, b: {x: 3, y: 4}}
let sums = {k: v["x"] + v["y"] for k, v in nested_data}
print("Sums:", sums)
if (sums["a"] != 3) { throw "sums['a'] should be 3" }
if (sums["b"] != 7) { throw "sums['b'] should be 7" }

print("\n=== Comprehensions with Tuples ===")

// Create tuples in comprehension
let pairs = [(i, i * 2) for i in range(4)]
print("Pairs count:", len(pairs))
if (pairs[0][0] != 0) { throw "pairs[0][0] should be 0" }
if (pairs[3][1] != 6) { throw "pairs[3][1] should be 6" }

// Named tuples
let points = [(x: i, y: i * i) for i in range(3)]
print("Points count:", len(points))
if (points[0].x != 0) { throw "points[0].x should be 0" }
if (points[2].y != 4) { throw "points[2].y should be 4" }

print("\n=== Performance Test ===")

// Large comprehension
let large = [x for x in range(1000) if x % 7 == 0]
print("Large comprehension elements:", len(large))
if (len(large) != 143) { throw "large should have 143 elements" }

// Nested performance
let big_matrix = [[i + j for j in range(20)] for i in range(20)]
print("Big matrix size:", len(big_matrix), "x", len(big_matrix[0]))
if (len(big_matrix) != 20) { throw "big_matrix should have 20 rows" }
if (len(big_matrix[0]) != 20) { throw "big_matrix[0] should have 20 columns" }

print("\nAll nested and advanced comprehension tests passed!")
