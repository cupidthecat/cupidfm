// Tuple destructuring tests

// Basic positional destructuring
let coords = (10, 20, 30)
let [x, y, z] = coords
print("Destructured x:", x, "y:", y, "z:", z)
if (x != 10) { throw "x should be 10" }
if (y != 20) { throw "y should be 20" }
if (z != 30) { throw "z should be 30" }

// Partial destructuring
let point = (100, 200, 300)
let [a, b] = point
print("Partial destructure a:", a, "b:", b)
if (a != 100) { throw "a should be 100" }
if (b != 200) { throw "b should be 200" }

// Destructuring with underscore (ignore values)
let data = (1, 2, 3, 4, 5)
let [first, _, third] = data
print("first:", first, "third:", third)
if (first != 1) { throw "first should be 1" }
if (third != 3) { throw "third should be 3" }

// Function returning tuple
fn get_position() {
    return (x: 100, y: 200)
}

let pos = get_position()
print("Function returned tuple:", pos)
if (pos.x != 100) { throw "x should be 100" }
if (pos.y != 200) { throw "y should be 200" }

// Destructure function return
let [px, py] = get_position()
print("Destructured function return px:", px, "py:", py)
if (px != 100) { throw "px should be 100" }
if (py != 200) { throw "py should be 200" }

// Nested tuples
let nested = ((1, 2), (3, 4))
print("Nested tuple:", nested)
if (nested[0][0] != 1) { throw "nested[0][0] should be 1" }
if (nested[0][1] != 2) { throw "nested[0][1] should be 2" }
if (nested[1][0] != 3) { throw "nested[1][0] should be 3" }
if (nested[1][1] != 4) { throw "nested[1][1] should be 4" }

// Tuple swap pattern using temporary tuple
let a_val = 10
let b_val = 20
let temp = (b_val, a_val)
let [new_a, new_b] = temp
a_val = new_a
b_val = new_b
print("After swap: a_val:", a_val, "b_val:", b_val)
if (a_val != 20) { throw "a_val should be 20 after swap" }
if (b_val != 10) { throw "b_val should be 10 after swap" }

print("All destructuring tests passed!")
