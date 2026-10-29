// Basic comprehension functionality test

// List comprehension
let squares = [x * x for x in range(5)]
if (len(squares) != 5) { throw "squares should have 5 elements" }
if (squares[0] != 0) { throw "squares[0] should be 0" }
if (squares[4] != 16) { throw "squares[4] should be 16" }

// Map comprehension with two variables
let data = {a: 1, b: 2, c: 3}
let doubled = {k: v * 2 for k, v in data}
if (len(doubled) != 3) { throw "doubled should have 3 elements" }
if (doubled.a != 2) { throw "doubled.a should be 2" }
if (doubled.c != 6) { throw "doubled.c should be 6" }

print("Basic comprehension tests passed!")
