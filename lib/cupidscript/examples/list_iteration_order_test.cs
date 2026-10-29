// Correct test program demonstrating enumerate() usage
// This file demonstrates what ACTUALLY works in CupidScript

print("=== LIST ITERATION WITH ENUMERATE() ===\n");

let colors = ["red", "green", "blue"];

// BASIC ITERATION: Single variable (values only)
print("BASIC: Single variable iteration (values only):");
for color in colors {
    print("  Color:", color);
}
print();

// WITH INDEX: Two-variable loops now supported!
print("WITH INDEX: Native two-variable syntax (value, index):");
for color, i in colors {
    print("  Index", i, "=", color, "(native syntax)");
}
print();

// Alternative: Using enumerate() with manual unpacking
print("ALTERNATIVE: Using enumerate() with manual unpacking:");
for pair in enumerate(colors) {
    let i = pair[0];
    let color = pair[1];
    print("  Index", i, "=", color, "(enumerate syntax)");
}
print();

// MAP ITERATION: Single variable gets keys
print("MAP ITERATION: Single variable gets keys:");
let scores = {"alice": 100, "bob": 85, "charlie": 92};
for name in scores {
    print("  Key:", name);
}
print();

// COMPREHENSIONS: Destructuring DOES work here
print("COMPREHENSIONS: Destructuring syntax works in comprehensions:");

let students = ["Alice", "Bob", "Carol"];

// List comprehension with destructuring
let labeled = [
    to_str(i) + ": " + name
    for [i, name] in enumerate(students)
];
print("  Labeled list:", labeled);

// Map comprehension with destructuring from enumerate
let student_ids = {
    name: i
    for [i, name] in enumerate(students)
};
print("  Student IDs:", student_ids);

// Map comprehension (no brackets needed for maps)
let passing = {
    name: score
    for name, score in scores
    if score >= 85
};
print("  Passing scores:", passing);
print();

// PRACTICAL EXAMPLES
print("PRACTICAL EXAMPLE 1: Finding first occurrence");
let nums = [5, 3, 8, 3, 9, 3];
let target = 3;

// Using native two-variable syntax
let first_index = -1;
for n, i in nums {
    if (n == target && first_index == -1) {
        first_index = i;
    }
}
print("  First occurrence of", target, "at index:", first_index);
print();

print("PRACTICAL EXAMPLE 2: Conditional formatting by position");
let values = [10, 20, 30, 40, 50];

let formatted = [
    (idx == 0 ? "FIRST: " : (idx == len(values) - 1 ? "LAST: " : "")) + to_str(val)
    for [idx, val] in enumerate(values)
];
print("  Formatted:", formatted);
print();

// EDGE CASES
print("EDGE CASES:");

print("  Empty list:");
for pair in enumerate([]) {
    let i = pair[0];
    let v = pair[1];
    print("    This won't print");
}
print("    (no iterations)");

print("  Single element:");
for pair in enumerate([42]) {
    let i = pair[0];
    let v = pair[1];
    print("    Index:", i, "Value:", v);
}

print("  Nested lists:");
let nested = [["a", "b"], ["c", "d"]];
for sublist, i in nested {
    print("    Sublist", i, ":", sublist);
    for item, j in sublist {
        print("      Item [" + to_str(i) + "][" + to_str(j) + "] =", item);
    }
}
print();

// COMPARISON: Different iteration approaches
print("COMPARISON: Four ways to iterate");
let items = ["A", "B", "C"];

print("1. Values only (simple):");
for item in items {
    print("   ", item);
}

print("2. Native two-variable syntax (value, index):");
for item, i in items {
    print("    [", i, ",", item, "]");
}

print("3. With enumerate() (manual unpacking - index, value):");
for pair in enumerate(items) {
    let i = pair[0];
    let item = pair[1];
    print("    [", i, ",", item, "]");
}

print("4. Manual counter (verbose, avoid this):");
let idx = 0;
for item in items {
    print("    [", idx, ",", item, "]");
    idx = idx + 1;
}
print();

print("=== KEY TAKEAWAYS ===");
print("1. For-loops: Use single variable for values only");
print("   for item in list { ... }");
print("2. Two-variable loops: Native syntax gives (value, index)");
print("   for item, i in list { ... }  // Note: value FIRST, index SECOND");
print("3. enumerate(): Returns list of [index, value] pairs");
print("   for pair in enumerate(list) { let i = pair[0]; let val = pair[1]; }");
print("4. Comprehensions: Destructuring works for both approaches");
print("   [item for item, i in list]  OR  [item for [i, item] in enumerate(list)]");
print("5. Maps: Single variable gets keys, two variables get (key, value)");
print("   for key in map { ... }  OR  for key, val in map { ... }");
print("6. When to use each:");
print("   - Native two-variable: Cleaner, more readable (value, index) or (key, value)");
print("   - enumerate(): When you specifically need [index, value] pairs as objects");
print();

print("=== TEST COMPLETE ===");
