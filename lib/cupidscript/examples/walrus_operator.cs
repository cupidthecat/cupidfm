// Walrus Operator (:=) Examples - Assignment Expressions
// The walrus operator allows you to assign and use a value in the same expression

print("=== Basic Walrus Operator ===");

// Simple assignment in condition
let x = 0;
if ((x := 10) > 5) {
    print("x is", x, "which is greater than 5");
}

// Avoid redundant function calls
fn expensive_operation(n) {
    print("Computing for", n);
    return n * n + n;
}

print("\n=== Avoiding Redundant Computation ===");

// Without walrus - calls function twice
let items = [5, 15, 25];
for item in items {
    if (expensive_operation(item) > 100) {
        print(item, "->", expensive_operation(item));  // Called again!
    }
}

// With walrus - calls function once
print("\nWith walrus operator:");
for item in items {
    if ((result := expensive_operation(item)) > 100) {
        print(item, "->", result);  // Reuse computed value
    }
}

// In comprehensions
print("\n=== Comprehensions with Walrus ===");

let threshold = 50;
let processed = [
    result
    for item in range(1, 15)
    if (result := expensive_operation(item)) > threshold
];
print("Processed values:", processed);

// Reading and processing data
print("\n=== Data Processing Pattern ===");

fn read_next_line() {
    // Simulate reading lines from a file
    let lines = ["Alice,25", "Bob,30", "", "Charlie,35", nil];
    let idx = 0;
    return fn() {
        if (idx >= len(lines)) { return nil; }
        let line = lines[idx];
        idx += 1;
        return line;
    };
}

let reader = read_next_line();
let valid_records = [];
let line = nil;

while ((line := reader()) != nil && line != "") {
    let parts = str_split(line, ",");
    if (len(parts) == 2) {
        push(valid_records, {
            "name": parts[0],
            "age": to_int(parts[1])
        });
    }
}

print("Valid records:");
for record in valid_records {
    print("  -", record["name"], "age", record["age"]);
}

// Pattern: process while fetching
print("\n=== Batch Processing Pattern ===");

fn get_batch(batch_num) {
    let batches = [
        [1, 2, 3],
        [4, 5, 6],
        [7, 8, 9],
        []
    ];
    if (batch_num < len(batches)) {
        return batches[batch_num];
    }
    return [];
}

let batch_num = 0;
let all_results = [];
let current_batch = nil;

while (len((current_batch := get_batch(batch_num))) > 0) {
    print("Processing batch", batch_num, ":", current_batch);
    for item in current_batch {
        push(all_results, item * 2);
    }
    batch_num += 1;
}

print("All results:", all_results);

// Regex-like matching pattern
print("\n=== Pattern Matching with Walrus ===");

fn find_pattern(text, pattern) {
    // Simulate finding a pattern in text
    if (str_find(text, pattern) >= 0) {
        return {
            "found": true,
            "position": str_find(text, pattern),
            "match": pattern
        };
    }
    return nil;
}

let texts = ["hello world", "foo bar baz", "test pattern here"];
let pattern = "pattern";

for text in texts {
    if ((result := find_pattern(text, pattern)) != nil) {
        print("Found '", result["match"], "' in '", text, "' at position", result["position"]);
    } else {
        print("Pattern not found in '", text, "'");
    }
}

// Validation with capture
print("\n=== Validation with Walrus ===");

fn validate_age(age) {
    if (age == nil || age < 0 || age > 150) { return nil; }
    return age;
}

let user_inputs = ["25", "200", "30", "-5", "abc"];

for input in user_inputs {
    let age_num = to_int(input);
    if ((validated := validate_age(age_num)) != nil) {
        print("Valid age:", validated);
    } else {
        print("Invalid age:", input);
    }
}

// Filtering with transformation
print("\n=== Filter + Transform in One Pass ===");

fn transform_if_valid(value) {
    if (value <= 0) { return nil; }
    return value * value;  // Square positive numbers
}

let numbers = [-5, 3, -2, 7, 0, 4];
let valid_squared = [
    squared
    for num in numbers
    if (squared := transform_if_valid(num)) != nil
];

print("Positive numbers squared:", valid_squared);

// State machine pattern
print("\n=== State Machine with Walrus ===");

fn get_next_state(current) {
    let transitions = {
        "start": "processing",
        "processing": "validating",
        "validating": "done",
        "done": nil
    };
    return transitions[current];
}

let state = "start";
let step_count = 0;

while ((state := get_next_state(state)) != nil) {
    step_count += 1;
    print("Step", step_count, "- State:", state);
}

print("State machine completed in", step_count, "steps");

// Resource allocation pattern
print("\n=== Resource Management ===");

fn allocate_resource(id) {
    if (id > 5) { return nil; }
    return {
        "id": id,
        "name": "Resource_" + to_str(id),
        "allocated": true
    };
}

let resources = [];
let next_id = 1;
let resource = nil;

while ((resource := allocate_resource(next_id)) != nil) {
    print("Allocated:", resource["name"]);
    push(resources, resource);
    next_id += 1;
}

print("Total resources allocated:", len(resources));

print("\n=== Walrus operator examples completed ===");
