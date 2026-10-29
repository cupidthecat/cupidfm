// Comprehension Conditional Expressions Examples
// Demonstrates Python-style "if-else" syntax in comprehensions

print("=== Comprehension Conditional Expressions ===\n")

// Example 1: Basic sign classification
print("1. Basic Sign Classification")
let numbers = [-5, -3, 0, 2, 7, -1, 4]
let signs = ["positive" if n > 0 else "negative" for n in numbers]
print("Numbers:", numbers)
print("Signs:", signs)
print()

// Example 2: Grade categorization
print("2. Pass/Fail Grades")
let scores = [95, 78, 82, 91, 67, 88, 45, 99]
let pass_fail = ["PASS" if score >= 70 else "FAIL" for score in scores]
print("Scores:", scores)
print("Pass/Fail:", pass_fail)
print()

// Example 3: Data validation messages
print("3. Data Validation")
let ages = [25, -5, 150, 30, 0, 18, 200]
let validations = [
    "valid" if age >= 0 && age <= 120 else "invalid"
    for age in ages
]
for i in range(len(ages)) {
    print("  Age", ages[i], "->", validations[i])
}
print()

// Example 4: Categorization
print("4. Size Categories")
let values = [5, 15, 25, 35, 45]
let categories = ["small" if v < 20 else "large" for v in values]
print("Values:", values)
print("Categories:", categories)
print()

// Example 5: Temperature classification
print("5. Temperature Check")
let temperatures = [-10, 0, 15, 25, 35, 42]
let climate = ["cold" if temp < 20 else "warm" for temp in temperatures]
for i in range(len(temperatures)) {
    print("  Temperature:", temperatures[i], "°C -", climate[i])
}
print()

// Example 6: Set comprehension with conditionals
print("6. Unique Classifications (Set)")
let vals = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10]
let parity_set = {"even" if n % 2 == 0 else "odd" for n in vals}
print("Unique parities:", parity_set)
print()

// Example 7: Map comprehension with conditional values
print("7. Number Properties (Map)")
let num_properties = {
    n: "even" if n % 2 == 0 else "odd"
    for n in range(1, 11)
}
print("Number properties:")
for pair in items(num_properties) {
    let k = pair[0]
    let v = pair[1]
    print(" ", k, ":", v)
}
print()

// Example 8: Filtering with conditionals
print("8. Filtered Status Messages")
let statuses = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10]
let active_status = [
    "active" if n % 3 == 0 else "inactive"
    for n in statuses
    if n > 3
]
print("Active status for n > 3:", active_status)
print()

// Example 9: Both syntaxes work (traditional ternary vs Python-style)
print("9. Syntax Comparison")
let nums = [1, 2, 3, 4, 5]

// Traditional ternary
let trad = [n % 2 == 0 ? "even" : "odd" for n in nums]
print("Traditional ternary:", trad)

// Python-style if-else
let python_style = ["even" if n % 2 == 0 else "odd" for n in nums]
print("Python-style if-else:", python_style)
print()

// Example 10: Multiple iterators with conditionals
print("10. Matrix Classification")
let matrix = [[1, 2], [3, 4], [5, 6]]
let even_odd = ["even" if x % 2 == 0 else "odd" for row in matrix for x in row]
print("Matrix elements classified:", even_odd)

print("\nAll examples completed!")
