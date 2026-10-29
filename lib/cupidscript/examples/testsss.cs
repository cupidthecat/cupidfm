// CupidScript Demo - Modern, Python-inspired scripting language

print("=== CupidScript Feature Showcase ===\n")

// 1. Basic Types & Operations
print("1. Basic Types")
let name = "CupidScript"
let version = 1.0
let active = true
print(fmt("Language: {}, Version: {}, Active: {}", name, version, active))

// 2. Collections
print("\n2. Collections")
let numbers = [1, 2, 3, 4, 5]
let config = {port: 8080, host: "localhost", debug: true}
let tags = {"frontend", "backend", "api"}

print(fmt("Numbers: {}", numbers))
print(fmt("Config: {}", config))
print(fmt("Tags: {}", tags))

// 3. List Comprehensions
print("\n3. List Comprehensions")
let squares = [x * x for x in numbers]
let evens = [x for x in numbers if x % 2 == 0]
print(fmt("Squares: {}", squares))
print(fmt("Evens: {}", evens))

// 4. Map/Set Comprehensions
print("\n4. Map/Set Comprehensions")
let doubled_map = {x: x * 2 for x in numbers}
let even_set = {x for x in numbers if x % 2 == 0}
print(fmt("Doubled map: {}", doubled_map))
print(fmt("Even set: {}", even_set))

// 5. For-in loops with TWO VARIABLES (now working!)
print("\n5. For-in loops with two variables")

// Loop with value and index
print("List iteration with index:")
for val, idx in numbers {
    print(fmt("  [{}] = {}", idx, val))
}

// Map iteration with key and value
print("\nMap iteration with key and value:")
for key, val in config {
    print(fmt("  {} = {}", key, val))
}

// 6. Destructuring patterns in for loops (now working!)
print("\n6. Destructuring patterns")

// List destructuring
print("List destructuring:")
for [val, idx] in numbers {
    print(fmt("  [{}] = {}", idx, val))
}

// Map destructuring
print("\nMap destructuring:")
for [k, v] in config {
    print(fmt("  {} = {}", k, v))
}

// 7. Functions with default parameters
print("\n7. Functions")
fn greet(name, greeting = "Hello") {
    return fmt("{}, {}!", greeting, name)
}

print(greet("World"))
print(greet("CupidScript", "Welcome"))

// 8. Arrow functions
print("\n8. Arrow functions")
let add = fn(a, b) => a + b
let multiply = fn(x, y) => {
    return x * y
}

print(fmt("5 + 3 = {}", add(5, 3)))
print(fmt("4 * 7 = {}", multiply(4, 7)))

// 9. Spread operator
print("\n9. Spread operator")
let first = [1, 2, 3]
let second = [4, 5, 6]
let combined = [...first, ...second]
print(fmt("Combined: {}", combined))

let defaults = {host: "0.0.0.0", port: 3000}
let overrides = {port: 8080, debug: true}
let final_config = {...defaults, ...overrides}
print(fmt("Final config: {}", final_config))

// 10. Pattern matching
print("\n10. Pattern matching")
fn describe(value) {
    return match(value) {
        case 0: "zero"
        case 1: "one"
        case x if x > 100: "large number"
        case [a, b]: fmt("pair: {}, {}", a, b)
        case {type: "user"}: "user object"
        default: "something else"
    }
}

print(describe(0))
print(describe(150))
print(describe([10, 20]))
print(describe({type: "user", name: "Alice"}))

// 11. Switch statement
print("\n11. Switch statement")
fn get_day_type(day) {
    switch(day) {
        case "Saturday", "Sunday" {
            return "weekend"
        }
        case "Monday", "Tuesday", "Wednesday", "Thursday", "Friday" {
            return "weekday"
        }
        default {
            return "unknown"
        }
    }
}

print(fmt("Saturday is a {}", get_day_type("Saturday")))
print(fmt("Monday is a {}", get_day_type("Monday")))

// 12. Error handling
print("\n12. Error handling")
fn safe_divide(a, b) {
    try {
        if b == 0 {
            throw "Division by zero"
        }
        return a / b
    } catch(e) {
        print(fmt("Error: {}", e))
        return nil
    }
}

print(fmt("10 / 2 = {}", safe_divide(10, 2)))
print(fmt("10 / 0 = {}", safe_divide(10, 0)))

// 13. Destructuring assignment
print("\n13. Destructuring assignment")
let [x, y, z] = [10, 20, 30]
print(fmt("x={}, y={}, z={}", x, y, z))

let {name: user_name, age: user_age} = {name: "Bob", age: 25, city: "NYC"}
print(fmt("User: {}, Age: {}", user_name, user_age))

// 14. Pipe operator
print("\n14. Pipe operator")
fn double(x) { return x * 2 }
fn add_ten(x) { return x + 10 }
fn to_string(x) { return str(x) }

let result = 5 |> double |> add_ten |> to_string
print(fmt("5 |> double |> add_ten |> to_string = '{}'", result))

// 15. Range operators
print("\n15. Range operators")
print("Exclusive range (0..5):")
for i in 0..5 {
    print(fmt("  {}", i))
}

print("Inclusive range (0..=5):")
for i in 0..=5 {
    print(fmt("  {}", i))
}

// 16. Walrus operator
print("\n16. Walrus operator (assignment expression)")
let items = [1, 2, 3, 4, 5]
let found = nil
for item in items {
    if (check := item > 3) {
        found = item
        break
    }
}
print(fmt("First item > 3: {}", found))

// 17. C-style for loops
print("\n17. C-style for loops")
print("Counting with C-style for:")
for (let i = 0; i < 5; i = i + 1) {
    print(fmt("  Count: {}", i))
}

// With walrus operator in init
print("\nWith walrus in condition:")
for (let i = 0; (doubled := i * 2) < 10; i = i + 1) {
    print(fmt("  i={}, doubled={}", i, doubled))
}

// 18. Tuples (immutable, fixed-size)
print("\n18. Tuples")

// Positional tuples
let coords = (10, 20, 30)
print(fmt("Coordinates: {}", coords))
print(fmt("X: {}, Y: {}, Z: {}", coords[0], coords[1], coords[2]))

// Named tuples
let point = (x: 100, y: 200)
print(fmt("Point: {}", point))
print(fmt("X: {}, Y: {}", point.x, point.y))

// Tuple destructuring
let [a, b, c] = coords
print(fmt("Destructured coords: a={}, b={}, c={}", a, b, c))

// 19. Optional chaining
print("\n19. Optional chaining")
let data = {user: {name: "Alice", profile: {age: 30}}}
let empty = {}

print(fmt("data?.user?.name = {}", data?.user?.name))
print(fmt("empty?.user?.name = {}", empty?.user?.name))

// 20. Nullish coalescing
print("\n20. Nullish coalescing")
let value1 = nil
let value2 = "exists"
print(fmt("nil ?? 'default' = {}", value1 ?? "default"))
print(fmt("'exists' ?? 'default' = {}", value2 ?? "default"))

print("\n=== Demo Complete ===")
