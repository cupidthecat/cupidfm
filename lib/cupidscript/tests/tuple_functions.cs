// Tuple function return tests

// Multiple return values using tuples
fn divide_with_remainder(a, b) {
    let quotient = floor(a / b)
    let remainder = a % b
    return (quotient: quotient, remainder: remainder)
}

let result = divide_with_remainder(17, 5)
print("17 / 5 = ", result.quotient, "remainder", result.remainder)
if (result.quotient != 3) { throw "quotient should be 3" }
if (result.remainder != 2) { throw "remainder should be 2" }

// Destructure return value
let [q, r] = divide_with_remainder(23, 7)
print("23 / 7 = ", q, "remainder", r)
if (q != 3) { throw "q should be 3" }
if (r != 2) { throw "r should be 2" }

// Return tuple from arrow function
let get_bounds = fn(arr) => {
    if (len(arr) == 0) { return (min: nil, max: nil) }
    let min_val = arr[0]
    let max_val = arr[0]
    for item in arr {
        if (item < min_val) { min_val = item }
        if (item > max_val) { max_val = item }
    }
    return (min: min_val, max: max_val)
}

let bounds = get_bounds([5, 2, 9, 1, 7])
print("bounds:", bounds)
if (bounds.min != 1) { throw "min should be 1" }
if (bounds.max != 9) { throw "max should be 9" }

// Tuple as function parameter
fn print_point(p) {
    print("Point: x =", p.x, ", y =", p.y)
}

print_point((x: 10, y: 20))

// Multiple tuple returns
fn get_stats(numbers) {
    let sum = 0
    let count = len(numbers)
    for n in numbers {
        sum = sum + n
    }
    let avg = count > 0 ? sum / count : 0
    return (sum: sum, count: count, average: avg)
}

let stats = get_stats([10, 20, 30, 40, 50])
print("Stats:", stats)
if (stats.sum != 150) { throw "sum should be 150" }
if (stats.count != 5) { throw "count should be 5" }
if (stats.average != 30) { throw "average should be 30" }

// Tuple in list
let points = [
    (x: 0, y: 0),
    (x: 10, y: 20),
    (x: 30, y: 40)
]

print("Points list:")
for p in points {
    print("  Point:", p.x, ",", p.y)
}

if (points[0].x != 0) { throw "first point x should be 0" }
if (points[1].y != 20) { throw "second point y should be 20" }
if (points[2].x != 30) { throw "third point x should be 30" }

print("All function tests passed!")
