// Enhanced Standard Library Examples
// Demonstrating practical usage of new utility functions

print("=== Enhanced Standard Library Examples ===\n");

// ===== String Formatting with Padding =====
print("--- String Formatting ---");
print("Invoice Example:");

fn format_invoice_line(item, quantity, price) {
    let item_col = str_pad_right(item, 20);
    let qty_col = str_pad_left(to_str(quantity), 5);
    // Format price with 2 decimal places manually
    let price_str = to_str(price);
    let price_col = str_pad_left(price_str, 10);
    return item_col + qty_col + price_col;
}

print(format_invoice_line("Product", "Qty", "Price"));
print(str_repeat("-", 35));
print(format_invoice_line("Widget A", 10, 29.99));
print(format_invoice_line("Gadget B", 5, 149.50));
print(format_invoice_line("Tool C", 2, 8.25));
print("");

// ===== Data Processing with list_chunk =====
print("--- Batch Processing with list_chunk ---");

let all_items = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10];
let batch_size = 3;
let batches = list_chunk(all_items, batch_size);

print("Processing " + to_str(len(all_items)) + " items in batches of " + to_str(batch_size) + ":");
for (let i = 0; i < len(batches); i = i + 1) {
    let batch = batches[i];
    print("  Batch " + to_str(i + 1) + ": " + to_str(batch));
}
print("");

// ===== Working with Nested Data using list_flatten =====
print("--- Flattening Nested Data ---");

let categories = [
    ["apple", "banana", "orange"],
    ["carrot", "broccoli"],
    ["chicken", "beef", "fish", "tofu"]
];

let all_foods = list_flatten(categories);
print("All foods: " + join(all_foods, ", "));
print("Total items: " + to_str(len(all_foods)));
print("");

// ===== Removing Duplicates with list_unique =====
print("--- Deduplication ---");

let survey_responses = ["yes", "no", "yes", "maybe", "yes", "no", "maybe"];
let unique_responses = list_unique(survey_responses);
print("Raw responses: " + to_str(survey_responses));
print("Unique responses: " + to_str(unique_responses));
print("");

// ===== Set Operations =====
print("--- Set Operations ---");

let team_a = ["Alice", "Bob", "Charlie", "Diana"];
let team_b = ["Charlie", "Diana", "Eve", "Frank"];

print("Team A: " + join(team_a, ", "));
print("Team B: " + join(team_b, ", "));
print("In both teams: " + join(list_intersection(team_a, team_b), ", "));
print("Only in Team A: " + join(list_difference(team_a, team_b), ", "));
print("All team members: " + join(list_union(team_a, team_b), ", "));
print("");

// ===== Cleaning Data with list_compact =====
print("--- Data Cleaning ---");

let raw_data = [42, nil, "hello", nil, 3.14, nil, true];
let clean_data = list_compact(raw_data);
print("Raw data with nils: " + to_str(raw_data));
print("Cleaned data: " + to_str(clean_data));
print("");

// ===== Partitioning Data =====
print("--- Data Partitioning ---");

let numbers = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10];
fn is_even(n) { return n % 2 == 0; }

let parts = list_partition(numbers, is_even);
print("Even numbers: " + to_str(parts[0]));
print("Odd numbers: " + to_str(parts[1]));
print("");

// ===== Quick Statistics with list_sum =====
print("--- Quick Statistics ---");

let sales = [150.50, 200.75, 99.99, 320.00, 75.25];
let total = list_sum(sales);
let average = total / len(sales);

print("Sales: " + to_str(sales));
print("Total: $" + fmt("%.2f", total));
print("Average: $" + fmt("%.2f", average));
print("");

// ===== Random Selection =====
print("--- Random Selection ---");

let prizes = ["Car", "Vacation", "TV", "Gift Card", "Hat"];
let winner = random_choice(prizes);
print("Available prizes: " + join(prizes, ", "));
print("Winner receives: " + winner + "!");

// Random team assignments
let players = ["Alice", "Bob", "Charlie", "Diana", "Eve", "Frank"];
let shuffled_players = copy(players);
shuffle(shuffled_players);
print("\nOriginal order: " + join(players, ", "));
print("Shuffled order: " + join(shuffled_players, ", "));

let team1 = take(shuffled_players, 3);
let team2 = drop(shuffled_players, 3);
print("Team 1: " + join(team1, ", "));
print("Team 2: " + join(team2, ", "));
print("");

// ===== Dice Roll Simulator =====
print("--- Dice Roll Simulator ---");
print("Rolling 2d6:");
let roll1 = random_int(1, 6);
let roll2 = random_int(1, 6);
print("Die 1: " + to_str(roll1));
print("Die 2: " + to_str(roll2));
print("Total: " + to_str(roll1 + roll2));
print("");

// ===== Deep Copy Example =====
print("--- Deep vs Shallow Copy ---");

let original = [
    "Config",
    ["database", "localhost", 5432],
    ["cache", "redis", 6379]
];

print("Original: " + to_str(original));

let shallow = copy(original);
let deep = deepcopy(original);

// Modify nested data
original[1][1] = "remote-server";

print("\nAfter modifying original[1][1] to 'remote-server':");
print("Original: " + to_str(original));
print("Shallow copy: " + to_str(shallow) + " (nested data changed!)");
print("Deep copy: " + to_str(deep) + " (nested data preserved!)");
print("");

// ===== Pagination Example =====
print("--- Pagination ---");

let all_records = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15];
let page_size = 5;
let page_num = 2;  // 0-indexed

let page = take(drop(all_records, page_num * page_size), page_size);
print("Page " + to_str(page_num + 1) + " (showing " + to_str(len(page)) + " of " + to_str(len(all_records)) + " records):");
print(to_str(page));
print("");

// ===== Text Alignment Example =====
print("--- Text Alignment ---");

fn print_aligned(left, center, right, width) {
    let total_width = width;
    let left_width = 15;
    let right_width = 15;
    let center_width = total_width - left_width - right_width;
    
    let left_col = str_pad_right(left, left_width);
    let center_col = str_pad_right(center, center_width);
    let right_col = str_pad_left(right, right_width);
    
    print(left_col + center_col + right_col);
}

print_aligned("Left", "Center", "Right", 50);
print(str_repeat("=", 50));
print_aligned("Page 1", "CupidScript Guide", "2024");
print("");

// ===== Working with contains =====
print("--- contains() Utility ---");

let inventory = ["apple", "banana", "orange", "grape"];
let shopping_list = ["banana", "milk", "bread"];

print("Checking shopping list against inventory:");
for (let i = 0; i < len(shopping_list); i = i + 1) {
    let item = shopping_list[i];
    if (contains(inventory, item)) {
        print("  ✓ " + item + " - In stock");
    } else {
        print("  ✗ " + item + " - Out of stock");
    }
}
print("");

// ===== Reverse for Display =====
print("--- Reversing Data ---");

let leaderboard = ["Alice", "Bob", "Charlie", "Diana"];
let reversed_board = reversed(leaderboard);

print("Top scores (ascending):");
for (let i = 0; i < len(leaderboard); i = i + 1) {
    print("  " + to_str(i + 1) + ". " + leaderboard[i]);
}

print("\nTop scores (descending):");
for (let i = 0; i < len(reversed_board); i = i + 1) {
    print("  " + to_str(i + 1) + ". " + reversed_board[i]);
}

print("\n=== Examples Complete ===");
