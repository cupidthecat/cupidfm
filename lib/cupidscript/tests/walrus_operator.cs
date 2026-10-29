// Test walrus operator (:=) - assignment expressions

print("Testing walrus operator...");

// Test 1: Basic walrus in if condition
let x = 0;
if ((x := 5) > 3) {
    assert(x == 5, "walrus assigns value in if");
    print("Test 1 passed: basic walrus in if");
}

// Test 2: Walrus with expression
let a = 0;
let b = (a := 10) + 5;
assert(a == 10, "walrus assigned to a");
assert(b == 15, "walrus value used in expression");
print("Test 2 passed: walrus in expression");

// Test 3: Walrus captures computed value
fn compute(x) {
    return x * 2;
}

let result = 0;
if ((result := compute(5)) > 8) {
    assert(result == 10, "walrus captured computed value");
    print("Test 3 passed: walrus with function call");
}

// Test 4: Nested walrus
let m = 0;
let n = 0;
if ((m := 5) > 3 && (n := m * 2) > 8) {
    assert(m == 5, "first walrus");
    assert(n == 10, "second walrus");
    print("Test 4 passed: nested walrus");
}

// Test 5: Walrus updates existing variable
let counter = 0;
let sum = (counter := counter + 1) + (counter := counter + 1);
assert(counter == 2, "walrus updated counter twice");
assert(sum == 3, "sum of walrus values: 1 + 2");
print("Test 5 passed: walrus updates");

// Test 6: Walrus in ternary
let score = 0;
let grade = (score := 85) >= 90 ? "A" : "B";
assert(score == 85, "walrus in ternary condition");
assert(grade == "B", "ternary used walrus value");
print("Test 6 passed: walrus in ternary");

// Test 7: Walrus preserves type
let float_val = 0.0;
if ((float_val := 3.14) > 3.0) {
    assert(typeof(float_val) == "float", "walrus preserves float type");
    print("Test 7 passed: type preservation");
}

// Test 8: Chained walrus
let chain1 = 0;
let chain2 = 0;
let final_val = (chain1 := (chain2 := 5) + 1);
assert(chain2 == 5, "innermost walrus");
assert(chain1 == 6, "outer walrus");
assert(final_val == 6, "chained walrus result");
print("Test 8 passed: chained walrus");

// Test 9: Walrus in while
let items = [1, 2, 3];
let i = 0;
let doubled = [];
let val = 0;
while (i < len(items) && (val := items[i] * 2) > 0) {
    push(doubled, val);
    i = i + 1;
}
assert(len(doubled) == 3, "walrus in while condition");
print("Test 9 passed: walrus in while");

// Test 10: Walrus with logical operators
let check1 = 0;
if ((check1 := 5) > 3 || (check1 := 10) > 20) {
    assert(check1 == 5, "short-circuit preserved first walrus");
    print("Test 10 passed: walrus with short-circuit");
}

print("\nAll walrus_operator tests passed!");
