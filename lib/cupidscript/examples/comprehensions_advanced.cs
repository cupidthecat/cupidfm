// Advanced Comprehension Patterns

print("=== Data Transformation Pipeline ===\n")

// Raw sensor data
let sensor_data = [
    {id: 1, temp: 72, humidity: 45},
    {id: 2, temp: 68, humidity: 52},
    {id: 3, temp: 75, humidity: 48},
    {id: 4, temp: 70, humidity: 50},
    {id: 5, temp: 73, humidity: 46}
]

// Extract temperatures
let temps = [reading["temp"] for reading in sensor_data]
print("Temperatures:", temps)

// Filter by condition
let hot_sensors = [s["id"] for s in sensor_data if s["temp"] > 72]
print("Hot sensor IDs:", hot_sensors)

// Create summary map
let summary = {
    s["id"]: "Temp: " + str(s["temp"]) + "°F, Humidity: " + str(s["humidity"]) + "%"
    for s in sensor_data
}
print("Summary:", summary)

print("\n=== Matrix Operations ===\n")

// Create identity matrix
let size = 4
let identity = [[i == j ? 1 : 0 for j in range(size)] for i in range(size)]
print("Identity matrix:")
for (let row : identity) {
    print("  ", row)
}

// Create diagonal matrix
let diagonal = [[i == j ? i + 1 : 0 for j in range(3)] for i in range(3)]
print("\nDiagonal matrix:")
for (let row : diagonal) {
    print("  ", row)
}

// Transpose (using nested comprehension)
let matrix = [[1, 2, 3], [4, 5, 6], [7, 8, 9]]
let transposed = [[matrix[i][j] for i in range(3)] for j in range(3)]
print("\nOriginal matrix:")
for (let row : matrix) {
    print("  ", row)
}
print("Transposed:")
for (let row : transposed) {
    print("  ", row)
}

print("\n=== Set-like Operations ===\n")

let list_a = [1, 2, 3, 4, 5]
let list_b = [4, 5, 6, 7, 8]

// Create map for fast lookup (set simulation)
let set_b = {x: true for i, x in list_b}

// Intersection (items in both)
let intersection = [x for x in list_a if set_b[x] == true]
print("Intersection:", intersection)

// Difference (items in A but not B)
let difference = [x for x in list_a if set_b[x] != true]
print("Difference (A - B):", difference)

print("\n=== Data Grouping ===\n")

let students = [
    {name: "Alice", grade: "A", score: 95},
    {name: "Bob", grade: "B", score: 85},
    {name: "Charlie", grade: "A", score: 92},
    {name: "Diana", grade: "C", score: 75},
    {name: "Eve", grade: "B", score: 88}
]

// Group by grade (simplified - takes first of each grade)
let by_grade = {s["grade"]: s["name"] for s in students}
print("One student per grade:", by_grade)

// Extract high performers
let high_performers = [s["name"] for s in students if s["score"] >= 90]
print("High performers (>= 90):", high_performers)

// Create grade report
let grade_report = {
    s["name"]: s["grade"] + " (" + str(s["score"]) + ")"
    for s in students
}
print("Grade report:", grade_report)

print("\n=== Text Processing ===\n")

let paragraph = "the quick brown fox jumps over the lazy dog"
let words_list = paragraph.split(" ")

// Word statistics
let word_stats = {
    word: {length: word.len(), first: word[0], last: word[word.len() - 1]}
    for i, word in words_list
}
print("\nWord statistics:")
for (let word, stats : word_stats) {
    print("  " + word + ":", stats)
}

// Filter words by length
let long_words = [w for w in words_list if w.len() > 3]
print("\nLong words (> 3 chars):", long_words)

// Create word index
let word_index = {word: i for i, word in words_list}
print("Word positions:", word_index)

print("\n=== Combinatorial Patterns ===\n")

// Cartesian product
let colors = ["red", "blue"]
let sizes = ["small", "large"]
let products = [{color: c, size: s} for c in colors for s in sizes]
print("Product combinations:")
for (let product : products) {
    print("  ", product)
}

// Pairs from single list
let numbers = [1, 2, 3]
let pairs = [(a, b) for a in numbers for b in numbers if a < b]
print("\nPairs (a < b):", pairs)

print("\n=== Functional Programming Style ===\n")

// Map-like transformation
let nums = range(1, 6)
let squared = [x * x for x in nums]
print("Map (square):", squared)

// Filter-like operation
let filtered = [x for x in nums if x % 2 == 1]
print("Filter (odd):", filtered)

// Map + Filter combined
let result = [x * x for x in range(1, 11) if x % 2 == 0]
print("Even squares:", result)

// Reduce simulation (sum)
fn sum_list(lst) {
    let total = 0
    for (let x : lst) {
        total = total + x
    }
    return total
}

let total = sum_list([x for x in range(1, 11)])
print("Sum 1-10:", total)

print("\n=== Complex Filtering ===\n")

let data_points = range(1, 101)

// Multiple predicates
let special = [
    x for x in data_points
    if x % 3 == 0
    if x % 5 != 0
    if x < 50
]
print("Special numbers (div by 3, not 5, < 50):", special)

// Prime-like filter (simple)
let candidates = range(2, 50)
let not_multiples_of_2 = [x for x in candidates if x == 2 or x % 2 != 0]
let not_multiples_of_3 = [x for x in not_multiples_of_2 if x == 3 or x % 3 != 0]
print("Prime candidates:", not_multiples_of_3)

print("\n=== Nested Map Comprehensions ===\n")

// Hierarchical data structure
let departments = {
    engineering: {alice: "senior", bob: "junior"},
    sales: {charlie: "manager", diana: "rep"},
    hr: {eve: "director"}
}

// Flatten to employee list
let all_employees = [name for dept, employees in departments for name, role in employees]
print("All employees:", all_employees)

// Count by department
let dept_sizes = {dept: {name: role for name, role in employees}.len() for dept, employees in departments}
print("Department sizes:", dept_sizes)

print("\n=== Performance Considerations ===\n")

// Lazy evaluation alternative would be generator (not implemented)
// But comprehensions are eager and create the full list/map

let big_range = range(10000)
let filtered_big = [x for x in big_range if x % 100 == 0]
print("Filtered large range (every 100th):", filtered_big.len(), "elements")

print("\nDone!")
