// List Destructuring in Comprehensions

print("============================================================")
print("LIST DESTRUCTURING IN COMPREHENSIONS")
print("============================================================")
print("")

// ============================================================
// 1. BASIC DESTRUCTURING
// ============================================================

print("1. Basic Destructuring")
print("----------------------------------------")

// Destructure pairs
let pairs = [[1, "a"], [2, "b"], [3, "c"]]
let swapped = [[v, k] for [k, v] in pairs]
print("Original pairs:", pairs)
print("Swapped:", swapped)
print("")

// Extract first elements
let firsts = [a for [a, b] in pairs]
print("First elements:", firsts)

// Extract second elements
let seconds = [b for [a, b] in pairs]
print("Second elements:", seconds)
print("")

// ============================================================
// 2. WITH ENUMERATE()
// ============================================================

print("2. With enumerate()")
print("----------------------------------------")

let fruits = ["apple", "banana", "cherry"]

// Standard (index, value) order with enumerate
let indexed = {idx: fruit for [idx, fruit] in enumerate(fruits)}
print("Indexed map:", indexed)

// Create labeled strings
let labeled = [to_str(i) + ": " + fruit for [i, fruit] in enumerate(fruits)]
print("Labeled:", labeled)

// Filter by index
let evens_only = [fruit for [i, fruit] in enumerate(fruits) if i % 2 == 0]
print("Even indices:", evens_only)
print("")

// ============================================================
// 3. COORDINATE PAIRS
// ============================================================

print("3. Coordinate Pairs")
print("----------------------------------------")

let coords = [[0, 0], [1, 2], [3, 4]]

// Calculate distances
let distances = [x * x + y * y for [x, y] in coords]
print("Squared distances:", distances)

// Filter by x coordinate
let positive_x = [[x, y] for [x, y] in coords if x > 0]
print("Positive X coords:", positive_x)

// Transform coordinates
let scaled = [[x * 2, y * 2] for [x, y] in coords]
print("Scaled 2x:", scaled)
print("")

// ============================================================
// 4. KEY-VALUE PAIRS
// ============================================================

print("4. Key-Value Pairs")
print("----------------------------------------")

// Convert list of pairs to map
let pairs_list = [["name", "Alice"], ["age", 30], ["city", "NYC"]]
let as_map = {k: v for [k, v] in pairs_list}
print("As map:", as_map)

// Filter pairs
let long_keys = [[k, v] for [k, v] in pairs_list if len(k) > 3]
print("Long keys:", long_keys)
print("")

// ============================================================
// 5. DATA PROCESSING
// ============================================================

print("5. Data Processing")
print("----------------------------------------")

// List of [id, info] pairs where info is a string
let records = [
    [1, "Alice-Engineer"],
    [2, "Bob-Designer"],
    [3, "Charlie-Manager"]
]

// Extract IDs
let ids = [id for [id, info] in records]
print("IDs:", ids)

// Extract info strings
let infos = [info for [id, info] in records]
print("Info strings:", infos)

// Create ID to info mapping
let id_map = {id: info for [id, info] in records}
print("ID map:", id_map)

// Filter by ID
let high_ids = [[id, info] for [id, info] in records if id > 1]
print("High IDs (>1):", high_ids)
print("")

// ============================================================
// 6. COMBINING WITH MULTIPLE ITERATORS
// ============================================================

print("6. Combining with Multiple Iterators")
print("----------------------------------------")

// Flatten list of pairs
let row1 = [[1, 2], [3, 4]]
let row2 = [[5, 6], [7, 8]]
let all_rows = [row1, row2]
let flat_x = [x for row in all_rows for [x, y] in row]
print("Flat values (x only):", flat_x)

// Cross product of pairs
let set1 = [[1, "a"], [2, "b"]]
let set2 = [[10, "x"], [20, "y"]]
let sums = [n1 + n2 for [n1, s1] in set1 for [n2, s2] in set2]
print("Sum of numbers:", sums)
print("")

// ============================================================
// 7. MAP COMPREHENSIONS
// ============================================================

print("7. Map Comprehensions with Destructuring")
print("----------------------------------------")

let data_pairs = [[1, "one"], [2, "two"], [3, "three"]]

// Swap to create map
let num_map = {n: w for [n, w] in data_pairs}
print("Number map:", num_map)

// Reverse map
let word_map = {w: n for [n, w] in data_pairs}
print("Word map:", word_map)

// Transform both
let transformed = {n * 10: upper(w) for [n, w] in data_pairs}
print("Transformed:", transformed)
print("")

// ============================================================
// 8. PRACTICAL EXAMPLES
// ============================================================

print("8. Practical Examples")
print("----------------------------------------")

// Transaction log
let transactions = [
    ["2024-01-15", 100],
    ["2024-01-16", -50],
    ["2024-01-17", 200]
]

// Extract dates
let dates = [date for [date, amount] in transactions]
print("Transaction dates:", dates)

// Calculate total (note: regular loops don't support destructuring)
let total = 0
for transaction in transactions {
    total = total + transaction[1]  // amount is at index 1
}
print("Transaction total:", total)

// Filter positive amounts
let positive = [amount for [date, amount] in transactions if amount > 0]
print("Positive amounts:", positive)

// Create date to amount map
let by_date = {date: amount for [date, amount] in transactions}
print("By date:", by_date)
print("")

print("============================================================")
print("All destructuring examples completed!")
print("============================================================")
