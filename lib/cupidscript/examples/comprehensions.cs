// Comprehensions - Basic examples

print("=== List Comprehensions ===\n")

// Simple squares
let squares = [x * x for x in range(10)]
print("Squares of 0-9:", squares)

// Even numbers
let evens = [x for x in range(20) if x % 2 == 0]
print("Even numbers 0-19:", evens)

// String manipulation
let words = ["hello", "world", "cupidscript"]
let uppercase = [w.upper() for w in words]
print("Uppercase words:", uppercase)

// String lengths
let lengths = [w.len() for w in words]
print("Word lengths:", lengths)

print("\n=== Map Comprehensions ===\n")

// Double values
let numbers = {a: 1, b: 2, c: 3, d: 4}
let doubled = {k: v * 2 for k, v in numbers}
print("Doubled values:", doubled)

// Filter by value
let high_values = {k: v for k, v in numbers if v > 2}
print("Values > 2:", high_values)

// Transform keys
let upper_keys = {k.upper(): v for k, v in numbers}
print("Uppercase keys:", upper_keys)

// From list to map
let items = ["apple", "banana", "cherry"]
let item_map = {i: item for i, item in items}
print("Item map:", item_map)

print("\n=== Nested Comprehensions ===\n")

// Multiplication table
let mult_table = [[i * j for j in range(1, 6)] for i in range(1, 6)]
print("5x5 multiplication table:")
for (let row : mult_table) {
    print("  ", row)
}

// Coordinate pairs
let coords = [(x, y) for x in range(3) for y in range(3)]
print("\nCoordinate pairs:", coords)

print("\n=== Practical Examples ===\n")

// Filter and transform data
let temperatures_f = [32, 68, 86, 104, 122]
let temps_c = [(f - 32) * 5 / 9 for f in temperatures_f]
print("Fahrenheit:", temperatures_f)
print("Celsius:", temps_c)

// Process student scores
let students = {
    alice: 85,
    bob: 92,
    charlie: 78,
    diana: 95,
    eve: 88
}

let passing = {name: score for name, score in students if score >= 80}
print("\nPassing students (>= 80):", passing)

let letter_grades = {
    name: score >= 90 ? "A" : score >= 80 ? "B" : score >= 70 ? "C" : "D"
    for name, score in students
}
print("Letter grades:", letter_grades)

// Word frequency (simplified)
let text_words = ["the", "quick", "brown", "fox", "the", "lazy", "dog", "the"]
let word_lengths = {word: word.len() for i, word in text_words}
print("\nUnique word lengths:", word_lengths)

// Create lookup table
let fruits = ["apple", "banana", "cherry", "date"]
let first_letters = {fruit[0]: fruit for i, fruit in fruits}
print("Fruits by first letter:", first_letters)

print("\n=== Combining with Other Features ===\n")

// With ranges
let fibonacci_approx = [i * i for i in range(10)]
print("Squares (Fibonacci approximation):", fibonacci_approx)

// With string interpolation
let names = ["Alice", "Bob", "Charlie"]
let greetings = ["Hello, ${name}!" for name in names]
print("Greetings:", greetings)

// With ternary operator
let mixed = [-5, -2, 0, 3, 7]
let abs_values = [x if x >= 0 else -x for x in mixed]
print("Absolute values:", abs_values)

// Chaining transformations
let raw_data = range(1, 11)
let processed = [x * 2 for x in [y for y in raw_data if y % 2 == 0]]
print("Even numbers doubled:", processed)

print("\nDone!")
