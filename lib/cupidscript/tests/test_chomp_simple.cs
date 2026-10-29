// Simple chomping test
print("Test 1: strip");
let d1 = yaml_parse("text: |-\n  line1\n  line2\n");
print("Result: '" + d1.text + "'");
print("Expected: 'line1\\nline2'");
if (d1.text == "line1\nline2") {
    print("PASS");
} else {
    print("FAIL - got length " + len(d1.text));
}

print("\nTest 2: clip");
let d2 = yaml_parse("text: |\n  line1\n  line2\n");
print("Result: '" + d2.text + "'");
print("Expected: 'line1\\nline2\\n'");
if (d2.text == "line1\nline2\n") {
    print("PASS");
} else {
    print("FAIL");
}

print("\nTest 3: keep");
let d3 = yaml_parse("text: |+\n  line1\n  line2\n\n");
print("Result: '" + d3.text + "'");
print("Expected: 'line1\\nline2\\n\\n'");
if (d3.text == "line1\nline2\n\n") {
    print("PASS");
} else {
    print("FAIL");
}

print("\nAll tests complete");
