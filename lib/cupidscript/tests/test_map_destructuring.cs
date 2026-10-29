// Test 1: Basic map destructuring
let user = { name: "Alice", age: 30, city: "NYC" }
let { name, age } = user
assert(name == "Alice", "Basic map destructuring: name")
assert(age == 30, "Basic map destructuring: age")
print("✓ Test 1: Basic map destructuring")

// Test 2: Map destructuring with same-name binding
let person = { first: "Bob", last: "Smith" }
let { first, last } = person
assert(first == "Bob", "Map destructuring: first")
assert(last == "Smith", "Map destructuring: last")
print("✓ Test 2: Map destructuring with same-name binding")

// Test 3: Rest pattern in map destructuring
let data = { x: 1, y: 2, z: 3, w: 4 }
let { x, y, ...rest } = data
assert(x == 1, "Rest pattern: x")
assert(y == 2, "Rest pattern: y")
assert(mhas(rest, "z"), "Rest pattern: has z")
assert(mhas(rest, "w"), "Rest pattern: has w")
assert(mget(rest, "z") == 3, "Rest pattern: z value")
assert(mget(rest, "w") == 4, "Rest pattern: w value")
assert(!mhas(rest, "x"), "Rest pattern: no x")
assert(!mhas(rest, "y"), "Rest pattern: no y")
print("✓ Test 3: Rest pattern in map destructuring")

// Test 4: Nested map destructuring
let company = {
    name: "TechCorp",
    location: {
        city: "SF",
        state: "CA"
    }
}
let { name: companyName } = company
let { location } = company
let { city } = location
assert(companyName == "TechCorp", "Nested destructuring: company name")
assert(city == "SF", "Nested destructuring: city")
print("✓ Test 4: Map destructuring with manual nesting")

// Test 5: Map destructuring with missing keys (should be nil)
let partial = { a: 1 }
let { a, b } = partial
assert(a == 1, "Partial destructuring: a")
assert(b == nil, "Partial destructuring: b is nil")
print("✓ Test 5: Map destructuring with missing keys")

// Test 6: Map destructuring with placeholder
let obj = { foo: "bar", baz: "qux" }
let { foo, _ } = obj
assert(foo == "bar", "Placeholder destructuring: foo")
print("✓ Test 6: Map destructuring with placeholder")

// Test 7: Multiple destructuring assignments
let obj1 = { a: 1, b: 2 }
let obj2 = { c: 3, d: 4 }
let { a } = obj1
let { c } = obj2
assert(a == 1, "Multiple destructuring: a")
assert(c == 3, "Multiple destructuring: c")
print("✓ Test 7: Multiple destructuring assignments")

// Test 8: Empty map destructuring
let empty = {}
let { notThere } = empty
assert(notThere == nil, "Empty map destructuring")
print("✓ Test 8: Empty map destructuring")

// Test 9: Map destructuring with rest at different positions
let full = { a: 1, b: 2, c: 3, d: 4, e: 5 }
let { a: first, ...middle } = full
assert(first == 1, "Rest position: first")
assert(len(keys(middle)) == 4, "Rest position: middle count")
print("✓ Test 9: Map destructuring with rest at different positions")

// Test 10: Map destructuring preserves key order in rest
let ordered = { first: 1, second: 2, third: 3 }
let { first: f, ...remaining } = ordered
let remainingKeys = keys(remaining)
assert(len(remainingKeys) == 2, "Rest order: key count")
print("✓ Test 10: Map destructuring preserves keys in rest")

// Test 11: Const map destructuring
const { name: userName } = { name: "Charlie" }
assert(userName == "Charlie", "Const map destructuring")
print("✓ Test 11: Const map destructuring")

// Test 12: Map destructuring extracts correct values
let config = { host: "localhost", port: 5432, key: "secret" }
let { host, port, key } = config
assert(host == "localhost", "Config: host")
assert(port == 5432, "Config: port")
assert(key == "secret", "Config: key")
print("✓ Test 12: Map destructuring extracts correct values")

// Test 13: Map destructuring with all rest
let allData = { x: 1, y: 2, z: 3 }
let { ...everything } = allData
assert(len(keys(everything)) == 3, "All rest: count")
assert(mget(everything, "x") == 1, "All rest: x")
assert(mget(everything, "y") == 2, "All rest: y")
assert(mget(everything, "z") == 3, "All rest: z")
print("✓ Test 13: Map destructuring with all rest")

print("\n✅ All map destructuring tests passed!")
