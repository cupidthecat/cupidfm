// Map Comprehensions with Complex Key Expressions
// Demonstrates the ability to use arbitrary expressions as keys

print("=== Basic Examples ===\n")

// Example 1: Transform keys with functions
let data = {apple: 5, banana: 3, cherry: 8}
let upper_keys = {upper(k): v for k, v in data}
print("Original:", data)
print("Upper case keys:", upper_keys)
// Result: {APPLE: 5, BANANA: 3, CHERRY: 8}

// Example 2: Add prefix/suffix to keys
let prefixed = {k + "_item": v for k, v in data}
print("With suffix:", prefixed)
// Result: {apple_item: 5, banana_item: 3, cherry_item: 8}

print("\n=== Numeric Key Transformations ===\n")

// Example 3: Swap and scale
let prices = {shirt: 20, pants: 40, shoes: 60}
let by_price = {v: k for k, v in prices}
print("By price:", by_price)
// Result: {20: "shirt", 40: "pants", 60: "shoes"}

let scaled_prices = {v * 100: k for k, v in prices}
print("Scaled by 100:", scaled_prices)
// Result: {2000: "shirt", 4000: "pants", 6000: "shoes"}

print("\n=== Complex Expressions ===\n")

// Example 4: Combine multiple operations
let inventory = {a: 10, b: 20, c: 30}
let transformed = {upper(k) + "_" + to_str(v): v * 2 for k, v in inventory}
print("Complex transform:", transformed)
// Result: {A_10: 20, B_20: 40, C_30: 60}

// Example 5: Conditional key generation
let scores = {alice: 95, bob: 85, charlie: 92}
let ranked = {v >= 90 ? k + "_honors" : k + "_regular": v for k, v in scores}
print("Ranked:", ranked)
// Result: {alice_honors: 95, bob_regular: 85, charlie_honors: 92}

print("\n=== Practical Use Cases ===\n")

// Use case 1: Create lookup table by property
let users = [
    {name: "Alice", age: 30},
    {name: "Bob", age: 25},
    {name: "Charlie", age: 35}
]

// Can't use complex keys directly with list comprehension,
// but we can iterate and transform using regular loop
let user_map = {}
for user in users {
    user_map[user["name"]] = user
}
print("User lookup:", user_map)

// Use case 2: Normalize keys to lowercase
let messy_data = {NAME: "Alice", AGE: 30, City: "NYC"}
let normalized = {lower(k): v for k, v in messy_data}
print("Normalized:", normalized)
// Result: {name: "Alice", age: 30, city: "NYC"}

// Use case 3: Create reverse mapping with transformation
let product_codes = {A1: "Widget", B2: "Gadget", C3: "Doohickey"}
let product_lookup = {lower(v): k for k, v in product_codes}
print("Product lookup:", product_lookup)
// Result: {widget: "A1", gadget: "B2", doohickey: "C3"}

print("\n=== With Filters ===\n")

// Example 6: Filter and transform keys
let items = {apple: 50, banana: 150, cherry: 75, date: 200}
let expensive_upper = {upper(k): v for k, v in items if v > 100}
print("Expensive items (upper):", expensive_upper)
// Result: {BANANA: 150, DATE: 200}

// Example 7: Complex filter and key expression
let readings = {sensor1: 22, sensor2: 45, sensor3: 18, sensor4: 39}
let alerts = {
    k + "_ALERT": v
    for k, v in readings
    if v > 20 && v < 40
}
print("Temperature alerts:", alerts)
// Result: {sensor1_ALERT: 22, sensor4_ALERT: 39}

print("\n=== Advanced Patterns ===\n")

// Pattern 1: Extract and transform nested data
let nested = {
    item1: {price: 10, qty: 5},
    item2: {price: 20, qty: 3},
    item3: {price: 15, qty: 7}
}

// Calculate totals with transformed keys
let totals = {k + "_total": v["price"] * v["qty"] for k, v in nested}
print("Totals:", totals)
// Result: {item1_total: 50, item2_total: 60, item3_total: 105}

// Pattern 2: Generate synthetic keys
let series = {a: 1, b: 2, c: 3, d: 4, e: 5}
let enumerated = {k + "_" + to_str(v): v * v for k, v in series}
print("Enumerated:", enumerated)
// Result: {a_1: 1, b_2: 4, c_3: 9, d_4: 16, e_5: 25}

print("\n=== Before and After Comparison ===\n")

// The same transformation shown two ways
let demo_data = {x: 10, y: 20, z: 30}

// After: Clean one-liner with complex key expression
let result = {upper(k): v * 2 for k, v in demo_data}
print("Result:", result)
// Result: {X: 20, Y: 40, Z: 60}

print("\nDone! Complex key expressions make comprehensions much more powerful.")
