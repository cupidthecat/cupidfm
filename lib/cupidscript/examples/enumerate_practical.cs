// Real-world practical examples using enumerate()
// Demonstrates common patterns and use cases

print("=== PRACTICAL ENUMERATE EXAMPLES ===\n");

// Example 1: CSV-like data with row numbers
print("Example 1: Adding row numbers to data");
let data = [
    {name: "Alice", score: 95},
    {name: "Bob", score: 87},
    {name: "Carol", score: 92},
    {name: "David", score: 78}
];

print("Leaderboard:");
for pair in enumerate(data) {
    let rank = pair[0];
    let entry = pair[1];
    print("  #" + to_str(rank + 1), "-", entry.name, ":", entry.score, "points");
}
print();

// Example 2: Finding multiple occurrences
print("Example 2: Finding all occurrences of a value");
let sequence = [3, 7, 3, 9, 3, 2, 3, 8];
let target = 3;

let positions = [idx for [idx, val] in enumerate(sequence) if val == target];
print("  Sequence:", sequence);
print("  Looking for:", target);
print("  Found at positions:", positions);
print();

// Example 3: Pairwise differences
print("Example 3: Computing differences between consecutive elements");
let prices = [100, 105, 102, 108, 110];
print("  Prices:", prices);
print("  Changes:");

for pair in enumerate(prices) {
    let idx = pair[0];
    let price = pair[1];
    if (idx > 0) {
        let prev_price = prices[idx - 1];
        let change = price - prev_price;
        let direction = change >= 0 ? "▲" : "▼";
        print("    Day", idx, ":", direction, change);
    }
}
print();

// Example 4: Creating alternating patterns
print("Example 4: Alternating formatting");
let items = ["Red", "Green", "Blue", "Yellow", "Purple"];
print("  Items with alternating prefixes:");

for pair in enumerate(items) {
    let i = pair[0];
    let item = pair[1];
    let prefix = i % 2 == 0 ? "→" : "  →";
    print(prefix, item);
}
print();

// Example 5: Skip first/last elements
print("Example 5: Processing all except first and last");
let values = [10, 20, 30, 40, 50, 60];
print("  All values:", values);
print("  Middle values only (skip first and last):");

let middle_values = [
    val 
    for [i, val] in enumerate(values) 
    if i > 0 && i < len(values) - 1
];
print("  ", middle_values);
print();

// Example 6: Creating numbered menu
print("Example 6: Interactive menu");
let menu_items = ["New File", "Open File", "Save File", "Exit"];
print("  Menu:");
for pair in enumerate(menu_items) {
    let i = pair[0];
    let item = pair[1];
    print("    [" + to_str(i) + "]", item);
}
print();

// Example 7: Batch processing with progress
print("Example 7: Batch processing simulation");
let files = ["doc1.txt", "doc2.txt", "doc3.txt", "doc4.txt", "doc5.txt"];
print("  Processing files:");

for pair in enumerate(files) {
    let i = pair[0];
    let filename = pair[1];
    let progress = ((i + 1) * 100) / len(files);
    print("    [" + to_str(progress) + "%] Processing:", filename);
}
print();

// Example 8: First/last detection
print("Example 8: Special formatting for first and last items");
let steps = ["Start", "Process", "Validate", "Complete"];
print("  Steps:");

for pair in enumerate(steps) {
    let i = pair[0];
    let step = pair[1];
    let marker = "";
    if (i == 0) {
        marker = "🟢 ";
    } else if (i == len(steps) - 1) {
        marker = "🏁 ";
    } else {
        marker = "   ";
    }
    print(marker, to_str(i + 1) + ".", step);
}
print();

// Example 9: Creating lookup tables
print("Example 9: Building bidirectional lookup tables");
let colors = ["red", "green", "blue"];

let color_to_index = {color: idx for [idx, color] in enumerate(colors)};
let index_to_color = {idx: color for [idx, color] in enumerate(colors)};

print("  Color to index:", color_to_index);
print("  Index to color:", index_to_color);
print("  Lookup 'green':", color_to_index["green"]);
print("  Lookup index 2:", index_to_color[2]);
print();

// Example 10: Grouping by position
print("Example 10: Grouping elements by even/odd position");
let all_items = ["A", "B", "C", "D", "E", "F", "G", "H"];

let even_positions = [item for [i, item] in enumerate(all_items) if i % 2 == 0];
let odd_positions = [item for [i, item] in enumerate(all_items) if i % 2 != 0];

print("  All items:", all_items);
print("  Even positions (0, 2, 4, ...):", even_positions);
print("  Odd positions (1, 3, 5, ...):", odd_positions);
print();

// Example 11: Adding ordinal suffixes
print("Example 11: Ordinal number formatting");
fn get_ordinal(n) {
    let suffix = "th";
    if (n % 10 == 1 && n % 100 != 11) {
        suffix = "st";
    } else if (n % 10 == 2 && n % 100 != 12) {
        suffix = "nd";
    } else if (n % 10 == 3 && n % 100 != 13) {
        suffix = "rd";
    }
    return to_str(n) + suffix;
}

let winners = ["Alice", "Bob", "Carol", "David"];
print("  Winners:");
for pair in enumerate(winners) {
    let i = pair[0];
    let name = pair[1];
    print("    " + get_ordinal(i + 1), "place:", name);
}
print();

// Example 12: Chunking with index awareness
print("Example 12: Processing in batches");
let tasks = ["T1", "T2", "T3", "T4", "T5", "T6", "T7", "T8"];
let batch_size = 3;

print("  Tasks:", tasks);
print("  Batch size:", batch_size);
print("  Batches:");

for pair in enumerate(tasks) {
    let i = pair[0];
    let task = pair[1];
    let batch_num = i / batch_size;
    let position_in_batch = i % batch_size;

    if (position_in_batch == 0) {
        print("    Batch", batch_num + 1, ":");
    }
    print("      -", task);
}
print();

// Example 13: Conditional accumulation
print("Example 13: Weighted sum based on position");
let nums = [10, 20, 30, 40, 50];
print("  Numbers:", nums);

// First element gets weight 1, second gets 2, etc.
let weighted_sum = 0;
for pair in enumerate(nums) {
    let i = pair[0];
    let num = pair[1];
    let weight = i + 1;
    weighted_sum = weighted_sum + (num * weight);
    print("    Position", i, ": value =", num, ", weight =", weight, ", contribution =", num * weight);
}
print("  Weighted sum:", weighted_sum);
print();

// Example 14: Creating indexed error messages
print("Example 14: Validation with line numbers");
let input_lines = ["valid", "valid", "ERROR", "valid", "WARNING", "valid"];

print("  Validation results:");
for pair in enumerate(input_lines) {
    let line_num = pair[0];
    let content = pair[1];
    if (content == "ERROR") {
        print("    ❌ Line", line_num + 1, ": ERROR detected");
    } else if (content == "WARNING") {
        print("    ⚠️  Line", line_num + 1, ": WARNING");
    } else {
        print("    ✓ Line", line_num + 1, ": OK");
    }
}
print();

// Example 15: Time-series analysis
print("Example 15: Stock price analysis");
let stock_prices = [100, 102, 101, 105, 107, 106, 110];

print("  Stock prices:", stock_prices);
print("  Analysis:");

let max_price = stock_prices[0];
let max_day = 0;
let min_price = stock_prices[0];
let min_day = 0;

for pair in enumerate(stock_prices) {
    let day = pair[0];
    let price = pair[1];
    if (price > max_price) {
        max_price = price;
        max_day = day;
    }
    if (price < min_price) {
        min_price = price;
        min_day = day;
    }
}

print("    Highest price:", max_price, "on day", max_day);
print("    Lowest price:", min_price, "on day", min_day);
print("    Gain:", max_price - min_price);
print();

print("=== EXAMPLES COMPLETE ===");
