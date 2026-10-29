// Debug chomping strip
let yaml = "text: |-\n  line1\n  line2\n";
print("Input YAML:");
print(yaml);

let d = yaml_parse(yaml);
print("Parsed text:");
print(d.text);

print("Expected: 'line1\\nline2'");
print("Got length: " + len(d.text));

// Print character by character
for i in range(len(d.text)) {
    let ch = substr(d.text, i, 1);
    if (ch == "\n") {
        print("  [" + i + "]: \\n (newline)");
    } else {
        print("  [" + i + "]: " + ch);
    }
}
