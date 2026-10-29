// String interpolation tests

let name = "Ada";
assert("Hello ${name}" == "Hello Ada", "basic interpolation");

assert("x=${1 + 2}" == "x=3", "expression interpolation");
assert("${1}x" == "1x", "leading interpolation");
assert("x${1}" == "x1", "trailing interpolation");
assert("${1}" == "1", "only interpolation");

assert("bool=${true}" == "bool=true", "bool interpolation");
assert("nil=${nil}" == "nil=nil", "nil interpolation");

let nums = [1, 2, 3];
assert("len=${len(nums)}" == "len=3", "function call in interpolation");

print("string_interpolation ok");
