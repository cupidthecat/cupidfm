// Advanced tuple patterns and use cases

print("=== Advanced Tuple Patterns ===\n")

// 1. Nested Tuples
print("1. Nested Tuples")
print("-" * 40)

let rect = (
    topLeft: (x: 0, y: 0),
    bottomRight: (x: 100, y: 50)
)

print("Rectangle:")
print("  Top-left:", rect.topLeft.x, ",", rect.topLeft.y)
print("  Bottom-right:", rect.bottomRight.x, ",", rect.bottomRight.y)

let width = rect.bottomRight.x - rect.topLeft.x
let height = rect.bottomRight.y - rect.topLeft.y
print("  Dimensions:", width, "x", height)

// 2. Tuple as Map Key
print("\n2. Tuples as Map Keys")
print("-" * 40)

let grid = {}
grid[(0, 0)] = "origin"
grid[(1, 0)] = "right"
grid[(0, 1)] = "up"
grid[(1, 1)] = "diagonal"

print("Grid positions:")
print("  (0,0):", grid[(0, 0)])
print("  (1,0):", grid[(1, 0)])
print("  (0,1):", grid[(0, 1)])
print("  (1,1):", grid[(1, 1)])

// 3. Builder Pattern
print("\n3. Builder Pattern with Tuples")
print("-" * 40)

fn create_user(data) {
    let defaults = (
        name: "Guest",
        email: "guest@example.com",
        age: 0,
        active: true
    )
    
    // Merge with provided data
    return (
        name: data.name ?? defaults.name,
        email: data.email ?? defaults.email,
        age: data.age ?? defaults.age,
        active: data.active ?? defaults.active
    )
}

let user1 = create_user((name: "Alice", email: "alice@example.com"))
print("User 1:", user1.name, "-", user1.email, "- age:", user1.age)

// 4. State Machine with Tuples
print("\n4. State Machine")
print("-" * 40)

fn process_state(state, action) {
    if (state.status == "idle" && action == "start") {
        return (status: "running", progress: 0, message: "Started")
    }
    if (state.status == "running" && action == "progress") {
        let new_progress = state.progress + 10
        if (new_progress >= 100) {
            return (status: "complete", progress: 100, message: "Done!")
        }
        return (status: "running", progress: new_progress, message: "In progress")
    }
    if (action == "stop") {
        return (status: "stopped", progress: state.progress, message: "Stopped")
    }
    return state
}

let state = (status: "idle", progress: 0, message: "Ready")
print("Initial:", state.status, "-", state.message)

state = process_state(state, "start")
print("After start:", state.status, "-", state.message, "-", state.progress + "%")

for (let i = 0; i < 5; i++) {
    state = process_state(state, "progress")
    print("Progress:", state.status, "-", state.progress + "%")
}

// 5. Point Operations
print("\n5. 2D Point Operations")
print("-" * 40)

fn point_add(p1, p2) {
    return (x: p1.x + p2.x, y: p1.y + p2.y)
}

fn point_distance(p1, p2) {
    let dx = p2.x - p1.x
    let dy = p2.y - p1.y
    return math.sqrt(dx * dx + dy * dy)
}

fn point_midpoint(p1, p2) {
    return (
        x: (p1.x + p2.x) / 2,
        y: (p1.y + p2.y) / 2
    )
}

let p1 = (x: 0, y: 0)
let p2 = (x: 30, y: 40)

print("Point 1:", p1.x, ",", p1.y)
print("Point 2:", p2.x, ",", p2.y)
print("Distance:", point_distance(p1, p2))

let mid = point_midpoint(p1, p2)
print("Midpoint:", mid.x, ",", mid.y)

// 6. HTTP Response Pattern
print("\n6. HTTP Response Pattern")
print("-" * 40)

fn mock_http_get(url) {
    if (url.contains("api")) {
        return (
            status: 200,
            body: '{"data": "success"}',
            headers: {"content-type": "application/json"}
        )
    }
    return (
        status: 404,
        body: "Not Found",
        headers: {"content-type": "text/plain"}
    )
}

let response = mock_http_get("https://api.example.com/data")
print("HTTP Response:")
print("  Status:", response.status)
print("  Body:", response.body)
print("  Type:", response.headers["content-type"])

// 7. Functional Programming with Tuples
print("\n7. Functional Patterns")
print("-" * 40)

// Map function that returns both transformed value and index
fn map_with_index(arr, fn) {
    let results = []
    for (let i = 0; i < arr.len(); i++) {
        results.push((value: fn(arr[i]), index: i))
    }
    return results
}

let numbers = [10, 20, 30, 40]
let doubled = map_with_index(numbers, fn(x) => x * 2)

print("Doubled with indices:")
for (let item : doubled) {
    print("  [" + item.index + "]:", item.value)
}

// 8. Version Information
print("\n8. Version Information")
print("-" * 40)

let version = (major: 1, minor: 2, patch: 3, build: "beta")

fn format_version(v) {
    return v.major + "." + v.minor + "." + v.patch + "-" + v.build
}

fn is_compatible(v1, v2) {
    return v1.major == v2.major
}

print("Version:", format_version(version))

let other = (major: 1, minor: 5, patch: 0, build: "release")
print("Other version:", format_version(other))
print("Compatible:", is_compatible(version, other))

print("\n=== Advanced Patterns Complete ===")
