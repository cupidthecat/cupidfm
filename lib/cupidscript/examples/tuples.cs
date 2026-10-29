// Tuple Examples - Demonstrating various tuple use cases

print("=== CupidScript Tuple Examples ===\n")

// 1. Basic Tuples
print("1. Basic Tuples")
print("-" * 40)

// Positional tuple
let coords = (10, 20, 30)
print("3D coordinates:", coords)
print("X:", coords[0], "Y:", coords[1], "Z:", coords[2])

// Named tuple
let person = (name: "Alice", age: 30, city: "NYC")
print("\nPerson:", person)
print("Name:", person.name)
print("Age:", person.age)
print("City:", person.city)

// 2. Function Returns
print("\n2. Multiple Return Values")
print("-" * 40)

fn divide_with_remainder(dividend, divisor) {
    return (
        quotient: dividend / divisor,
        remainder: dividend % divisor
    )
}

let result = divide_with_remainder(17, 5)
print("17 ÷ 5 =", result.quotient, "remainder", result.remainder)

// Destructure the return value
let [q, r] = divide_with_remainder(23, 7)
print("23 ÷ 7 =", q, "remainder", r)

// 3. Destructuring
print("\n3. Destructuring")
print("-" * 40)

let point = (x: 100, y: 200)
let [x, y] = point
print("Extracted x:", x, "y:", y)

// Swap values using tuple
let a = 5
let b = 10
print("Before swap: a =", a, "b =", b)
[a, b] = (b, a)
print("After swap:  a =", a, "b =", b)

// 4. Data Structures
print("\n4. Tuples as Data Structures")
print("-" * 40)

// RGB color as tuple
let color = (r: 255, g: 128, b: 64, a: 1.0)
print("Color RGBA:", color.r, color.g, color.b, color.alpha)

// Date as tuple
let today = (year: 2026, month: 1, day: 27)
print("Date:", today.year + "-" + today.month + "-" + today.day)

// 5. Collections of Tuples
print("\n5. Collections of Tuples")
print("-" * 40)

let contacts = [
    (name: "Alice", phone: "555-0001"),
    (name: "Bob", phone: "555-0002"),
    (name: "Charlie", phone: "555-0003")
]

print("Contact List:")
for (let contact : contacts) {
    print("  " + contact.name + ": " + contact.phone)
}

// 6. Configuration with Tuples
print("\n6. Configuration Data")
print("-" * 40)

fn create_window(config) {
    print("Creating window:")
    print("  Size:", config.width, "x", config.height)
    print("  Position:", config.x, ",", config.y)
    print("  Title:", config.title)
}

create_window((
    width: 800,
    height: 600,
    x: 100,
    y: 100,
    title: "My Application"
))

// 7. Error Handling Pattern
print("\n7. Result Pattern")
print("-" * 40)

fn safe_divide(a, b) {
    if (b == 0) {
        return (ok: false, error: "Division by zero")
    }
    return (ok: true, value: a / b)
}

let res1 = safe_divide(10, 2)
if (res1.ok) {
    print("Result:", res1.value)
} else {
    print("Error:", res1.error)
}

let res2 = safe_divide(10, 0)
if (res2.ok) {
    print("Result:", res2.value)
} else {
    print("Error:", res2.error)
}

// 8. Statistical Data
print("\n8. Statistical Analysis")
print("-" * 40)

fn calculate_stats(numbers) {
    if (numbers.len() == 0) {
        return (min: nil, max: nil, avg: nil, sum: 0)
    }
    
    let min = numbers[0]
    let max = numbers[0]
    let sum = 0
    
    for (let n : numbers) {
        if (n < min) min = n
        if (n > max) max = n
        sum = sum + n
    }
    
    return (
        min: min,
        max: max,
        avg: sum / numbers.len(),
        sum: sum
    )
}

let data = [15, 23, 8, 42, 16, 31, 4]
let stats = calculate_stats(data)
print("Data:", data)
print("Statistics:")
print("  Min:", stats.min)
print("  Max:", stats.max)
print("  Average:", stats.avg)
print("  Sum:", stats.sum)

// 9. Range/Bounds
print("\n9. Bounds and Ranges")
print("-" * 40)

fn get_bounds(arr) {
    if (arr.len() == 0) {
        return (start: nil, end: nil, length: 0)
    }
    return (start: arr[0], end: arr[arr.len() - 1], length: arr.len())
}

let sequence = [10, 20, 30, 40, 50]
let bounds = get_bounds(sequence)
print("Sequence:", sequence)
print("Bounds: start =", bounds.start, "end =", bounds.end, "length =", bounds.length)

print("\n=== All Examples Complete ===")
