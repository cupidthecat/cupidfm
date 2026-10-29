// Demonstration of enumerate() function and list iteration order
// Shows the difference between native (value, index) and enumerate's (index, value)

print("=== ENUMERATE DEMONSTRATION ===\n");

// Basic enumerate usage
print("1. Basic enumerate() usage:");
let fruits = ["apple", "banana", "cherry", "date"];
print("  fruits =", fruits);
print("  enumerate(fruits) =", enumerate(fruits));
print();

// Iterating with enumerate - recommended approach
print("2. Iterating with enumerate() - RECOMMENDED:");
print("  Note: In for loops, access pair[0] for index, pair[1] for value");
for pair in enumerate(fruits) {
    let idx = pair[0];
    let fruit = pair[1];
    print("  Index", idx, "->", fruit);
}
print();

// Single variable iteration (values only)
print("3. Single variable iteration (values only):");
for fruit in fruits {
    print("  fruit =", fruit);
}
print();

// Using enumerate in comprehensions
print("4. enumerate() in list comprehensions:");
let labeled = [to_str(idx) + ": " + fruit for [idx, fruit] in enumerate(fruits)];
print("  labeled =", labeled);
print();

// Creating index-to-value maps
print("5. Creating maps with enumerate():");
let index_map = {idx: fruit for [idx, fruit] in enumerate(fruits)};
print("  index_map =", index_map);
print();

// Creating value-to-index maps
print("6. Creating reverse maps (value to index):");
let reverse_map = {fruit: idx for [idx, fruit] in enumerate(fruits)};
print("  reverse_map =", reverse_map);
print();

// Filtering based on index
print("7. Filtering by index (even indices only):");
let even_indices = [fruit for [idx, fruit] in enumerate(fruits) if idx % 2 == 0];
print("  even_indices =", even_indices);
print();

// Filtering based on index (odd indices only)
print("8. Filtering by index (odd indices only):");
let odd_indices = [fruit for [idx, fruit] in enumerate(fruits) if idx % 2 != 0];
print("  odd_indices =", odd_indices);
print();

// Complex transformations with index and value
print("9. Complex transformations:");
let formatted = [
    "#" + to_str(idx + 1) + " - " + upper(fruit) 
    for [idx, fruit] in enumerate(fruits)
];
print("  formatted =", formatted);
print();

// Using enumerate with numbers
print("10. enumerate() with numeric list:");
let numbers = [10, 20, 30, 40, 50];
for pair in enumerate(numbers) {
    let i = pair[0];
    let val = pair[1];
    print("  Position", i, "has value", val, "(doubled:", val * 2, ")");
}
print();

// Nested structures with enumerate
print("11. Nested lists with enumerate:");
let matrix = [
    [1, 2, 3],
    [4, 5, 6],
    [7, 8, 9]
];

for row_pair in enumerate(matrix) {
    let row_idx = row_pair[0];
    let row = row_pair[1];
    for col_pair in enumerate(row) {
        let col_idx = col_pair[0];
        let val = col_pair[1];
        print("  matrix[" + to_str(row_idx) + "][" + to_str(col_idx) + "] =", val);
    }
}
print();

// Comparison with native two-variable syntax
print("12. COMPARISON: Native two-variable syntax vs enumerate():");
print("  CupidScript now supports both approaches!\n");

print("  A) Native two-variable syntax (value, index) in loops:");
for fruit, idx in fruits {
    print("    Index", idx, "->", fruit, "(native syntax)");
}
print();

print("  B) enumerate() with manual unpacking (index, value):");
for pair in enumerate(fruits) {
    let idx = pair[0];
    let fruit = pair[1];
    print("    Index", idx, "->", fruit, "(enumerate syntax)");
}
print();

print("  When to use each:");
print("    - Native syntax: Cleaner, more readable (value, index) order");
print("    - enumerate(): When you need [index, value] pairs or (index, value) order");
print("    - Both work in loops! Comprehensions support destructuring for both!");
print();

print("  Native syntax in comprehension:");
let native_labeled = [to_str(idx) + ": " + fruit for fruit, idx in fruits];
print("    [to_str(idx) + ': ' + fruit for fruit, idx in fruits]");
print("    =", native_labeled);
print();

print("  enumerate() in comprehension (destructuring works here!):");
let enum_labeled = [to_str(idx) + ": " + fruit for [idx, fruit] in enumerate(fruits)];
print("    [to_str(idx) + ': ' + fruit for [idx, fruit] in enumerate(fruits)]");
print("    =", enum_labeled);
print();

print("  Both produce the same result, choose based on preference!");
print();

// Practical example: Finding position of maximum value
print("13. Practical example - finding max value position:");
let scores = [85, 92, 78, 95, 88];
print("  scores =", scores);

print("  Using native two-variable syntax:");
let max_score = -1;
let max_idx = -1;
for score, idx in scores {
    if (score > max_score) {
        max_score = score;
        max_idx = idx;
    }
}
print("    Maximum score:", max_score, "at index", max_idx);
print();

// enumerate() with empty list
print("14. enumerate() with empty list:");
let empty = [];
print("  enumerate([]) =", enumerate(empty));
print();

// enumerate() with single element
print("15. enumerate() with single element:");
let single = ["only"];
print("  enumerate(['only']) =", enumerate(single));
print();

// Using enumerate to create numbered pairs
print("16. Creating numbered pairs:");
let items = ["task1", "task2", "task3"];
let numbered = [
    {num: idx + 1, task: item} 
    for [idx, item] in enumerate(items)
];
for obj in numbered {
    print("  Task #" + to_str(obj.num) + ":", obj.task);
}
print();

print("=== DEMONSTRATION COMPLETE ===");
