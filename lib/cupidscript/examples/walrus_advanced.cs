// Advanced Walrus Operator Patterns

print("=== Advanced Walrus Operator Patterns ===\n");

// 1. Pipeline processing with early exit
print("1. Pipeline Processing with Validation");

fn parse_json_safe(text) {
    // Simulate JSON parsing that might fail
    if (str_find(text, "{") < 0) { return nil; }
    return {"parsed": true, "data": text};
}

fn validate_data(obj) {
    if (obj == nil || !obj["parsed"]) { return nil; }
    return {"valid": true, "payload": obj["data"]};
}

fn process_payload(validated) {
    if (validated == nil) { return nil; }
    return "Processed: " + validated["payload"];
}

let inputs = ["{\"valid\": 1}", "invalid", "{\"valid\": 2}"];

for input in inputs {
    // Chain operations with walrus, exit early on nil
    if ((parsed := parse_json_safe(input)) != nil &&
        (validated := validate_data(parsed)) != nil &&
        (result := process_payload(validated)) != nil) {
        print("Success:", result);
    } else {
        print("Failed to process:", input);
    }
}

// 2. Memoization pattern
print("\n2. Memoization with Walrus");

let cache = map();

fn fibonacci(n) {
    if (n <= 1) { return n; }
    
    // Check cache with walrus
    let cache_key = to_str(n);
    if ((cached := cache[cache_key]) != nil) {
        return cached;
    }
    
    // Compute and cache
    let result = fibonacci(n - 1) + fibonacci(n - 2);
    cache[cache_key] = result;
    return result;
}

for i in range(10) {
    print("fib(", i, ") =", fibonacci(i));
}
print("Cache size:", len(keys(cache)));

// 3. Retry logic with timeout
print("\n3. Retry Logic with Backoff");

fn unstable_operation(attempt) {
    // Simulate operation that succeeds on 3rd try
    if (attempt < 3) { return nil; }
    return {"success": true, "attempt": attempt};
}

let max_retries = 5;
let attempt = 0;
let result = nil;

while (attempt < max_retries && (result := unstable_operation(attempt)) == nil) {
    print("Attempt", attempt + 1, "failed, retrying...");
    attempt += 1;
}

if (result != nil) {
    print("Operation succeeded on attempt", result["attempt"] + 1);
} else {
    print("Operation failed after", max_retries, "attempts");
}

// 4. Stream processing with windowing
print("\n4. Stream Processing with Window");

fn moving_average(values, window_size) {
    let results = [];
    let window = [];
    
    for value in values {
        push(window, value);
        
        // Maintain window size with walrus
        if ((win_len := len(window)) > window_size) {
            remove(window, 0);
        }
        
        // Calculate average if window is full
        if (len(window) == window_size) {
            let sum = reduce(window, fn(acc, x) => acc + x, 0);
            push(results, sum / window_size);
        }
    }
    
    return results;
}

let data_stream = [10, 20, 30, 40, 50, 60, 70, 80];
let averages = moving_average(data_stream, 3);
print("Moving averages (window=3):", averages);

// 5. Nested data extraction with safety
print("\n5. Safe Nested Data Access");

let data = {
    "users": [
        {"name": "Alice", "address": {"city": "NYC"}},
        {"name": "Bob"},
        {"name": "Charlie", "address": {"city": "SF"}}
    ]
};

let cities = [
    city
    for user in data["users"]
    if (addr := user["address"]) != nil &&
       (city := addr["city"]) != nil
];

print("Cities:", cities);

// 6. Pagination with walrus
print("\n6. Pagination Pattern");

fn get_page(page_num, page_size) {
    let all_items = range(100);  // Simulate 100 items
    let start = page_num * page_size;
    let end = start + page_size;
    
    if (start >= len(all_items)) { return []; }
    return slice(all_items, start, end);
}

let page_num = 0;
let page_size = 10;
let all_results = [];
let page = nil;

while (len((page := get_page(page_num, page_size))) > 0) {
    print("Page", page_num + 1, ":", len(page), "items");
    extend(all_results, page);
    page_num += 1;
}

print("Total items fetched:", len(all_results));

// 7. Complex filtering with multiple criteria
print("\n7. Multi-Criteria Filtering");

let products = [
    {"name": "Widget", "price": 10, "stock": 5},
    {"name": "Gadget", "price": 25, "stock": 0},
    {"name": "Doohickey", "price": 15, "stock": 10},
    {"name": "Thingamajig", "price": 30, "stock": 3}
];

// Filter products: in stock, discounted price > 10
let filtered = [
    {
        "name": p["name"],
        "discounted": discounted
    }
    for p in products
    if p["stock"] > 0 &&
       (discounted := p["price"] * 0.9) > 10
];

print("Filtered products:");
for p in filtered {
    print("  -", p["name"], "at $", p["discounted"]);
}

// 8. Circuit breaker pattern
print("\n8. Circuit Breaker Pattern");

let failure_count = 0;
let circuit_open = false;
let threshold = 3;

fn risky_operation(should_fail) {
    if (circuit_open) {
        return {"error": "Circuit open"};
    }
    
    if (should_fail) {
        failure_count += 1;
        if ((fc := failure_count) >= threshold) {
            circuit_open = true;
            print("Circuit breaker opened after", fc, "failures");
        }
        return nil;
    }
    
    failure_count = 0;  // Reset on success
    return {"success": true};
}

let test_sequence = [true, true, true, false, false];

for should_fail in test_sequence {
    let result = risky_operation(should_fail);
    if (result != nil) {
        print("Operation result:", result);
    } else {
        print("Operation failed");
    }
}

// 9. Token bucket rate limiter
print("\n9. Rate Limiting with Token Bucket");

let bucket_capacity = 5;
let tokens = bucket_capacity;
let refill_rate = 1;

fn try_consume(count) {
    if ((available := tokens) >= count) {
        tokens -= count;
        return true;
    }
    return false;
}

fn refill() {
    if ((current := tokens) < bucket_capacity) {
        tokens = min(current + refill_rate, bucket_capacity);
    }
}

// Simulate requests
let requests = [1, 2, 1, 3, 2, 1];  // Token counts needed

for req in requests {
    if (try_consume(req)) {
        print("Request for", req, "tokens allowed. Remaining:", tokens);
    } else {
        print("Request for", req, "tokens denied. Refilling...");
        refill();
    }
}

print("\n=== Advanced patterns completed ===");
