print("=== stress ===");

// precedence: * before +
print("1 + 2 * 3 =", 1 + 2 * 3);          // 7
print("(1 + 2) * 3 =", (1 + 2) * 3);      // 9

// comparisons + chaining style
print("7 == 7 =", 7 == 7);
print("7 != 8 =", 7 != 8);
print("3 < 5 =", 3 < 5);
print("5 <= 5 =", 5 <= 5);
print("9 > 1 =", 9 > 1);
print("9 >= 10 =", 9 >= 10);

// short-circuit should avoid division-by-zero
print("false && (1/0) =", false && (1 / 0));
print("true  || (1/0) =", true  || (1 / 0));

// unary operators
print("!true =", !true);
print("!-5 truthy check =", !(-5)); // -5 is truthy, so !(-5) == false

// string escapes
print("escapes:", "line1\nline2\t\"quoted\"\\slash");

// while loop with string building
let i = 0;
let acc = "";
while (i < 5) {
  acc = acc + i; // int -> string via +
  i = i + 1;
}
print("acc =", acc); // "01234"

// function closure-ish sanity (uses global)
let G = 100;
fn addG(x) { return x + G; }
print("addG(23) =", addG(23)); // 123

// nil behavior
let n = nil;
print("n == nil =", n == nil);
print("n != nil =", n != nil);

print("=== stress done ===");
