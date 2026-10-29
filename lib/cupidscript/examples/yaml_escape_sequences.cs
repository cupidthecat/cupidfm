// YAML Escape Sequences - Complete Guide for YAML 1.2.2
// Demonstrates all escape sequences supported in double-quoted YAML strings

print("=== YAML Escape Sequences Demo ===\n");

// ============================================================================
// COMMON ESCAPE SEQUENCES
// ============================================================================

print("--- Common Escapes ---");

// Backslash and quotes
let d1 = yaml_parse("path: \"C:\\\\Users\\\\Documents\"\n");
print("Windows path:", d1.path);  // C:\Users\Documents

let d2 = yaml_parse("quote: \"He said \\\"hello\\\"\"\n");
print("Quoted text:", d2.quote);  // He said "hello"

// Whitespace characters
let d3 = yaml_parse("text: \"Tab\\there\\nNew\\rline\"\n");
print("Whitespace:", d3.text);
// Shows: Tab	here
//        New
//        line

// Special control characters
let d4 = yaml_parse("control: \"\\0 null \\a bell \\b backspace \\e escape\"\n");
print("Control chars:", d4.control);

print("");

// ============================================================================
// HEX ESCAPES (\xHH) - YAML 1.2.2
// ============================================================================

print("--- Hex Escapes (\\xHH) ---");

// Basic ASCII characters
let d5 = yaml_parse("text: \"\\x41\\x42\\x43\"\n");  // A B C
print("ABC via hex:", d5.text);

// Whitespace via hex
let d6 = yaml_parse("text: \"Hello\\x20World\\x09Tab\"\n");
print("Hex whitespace:", d6.text);  // "Hello World	Tab"

// Special characters
let d7 = yaml_parse("text: \"\\x21\\x40\\x23\"\n");  // ! @ #
print("Special chars:", d7.text);

// Null bytes (careful with display)
let d8 = yaml_parse("data: \"before\\x00after\"\n");
print("With null byte (length):", len(d8.data));  // 12 (includes null)

// High bytes (produces UTF-8)
let d9 = yaml_parse("text: \"\\xFF \\xF0\"\n");
print("High bytes:", d9.text);

print("");

// ============================================================================
// UNICODE ESCAPES
// ============================================================================

print("--- Unicode Escapes ---");

// 4-digit Unicode (\uXXXX)
let d10 = yaml_parse("text: \"\\u0048\\u0065\\u006c\\u006c\\u006f\"\n");
print("Hello via \\u:", d10.text);  // Hello

// 8-digit Unicode (\UXXXXXXXX)
let d11 = yaml_parse("emoji: \"\\U0001F44B\"\n");
print("Waving hand:", d11.emoji);  // 👋

// Multi-language text
let d12 = yaml_parse("
chinese: \"\\u4F60\\u597D\"
arabic: \"\\u0645\\u0631\\u062D\\u0628\\u0627\"
russian: \"\\u041F\\u0440\\u0438\\u0432\\u0435\\u0442\"
");
print("Chinese:", d12.chinese);  // 你好
print("Arabic:", d12.arabic);    // مرحبا
print("Russian:", d12.russian);  // Привет

// Mixing escape types
let d13 = yaml_parse("mixed: \"\\x41\\u0042\\U00000043\"\n");
print("Mixed ABC:", d13.mixed);  // ABC

print("");

// ============================================================================
// SPECIAL WHITESPACE ESCAPES (YAML 1.2.2)
// ============================================================================

print("--- Special Whitespace (YAML 1.2.2) ---");

// Next Line (NEL) - U+0085
let d14 = yaml_parse("text: \"Line 1\\NLine 2\\NLine 3\"\n");
print("Next Line (\\N):");
print(d14.text);
print("(Length:", len(d14.text) + ")");

// Non-Breaking Space (NBSP) - U+00A0
let d15 = yaml_parse("text: \"No\\_Break\\_Space\\_Here\"\n");
print("\\nNon-Breaking Space (\\_):");
print(d15.text);
print("(Length:", len(d15.text) + ")");

// Line Separator (LS) - U+2028
let d16 = yaml_parse("text: \"Paragraph 1\\LParagraph 2\"\n");
print("\\nLine Separator (\\L):");
print(d16.text);

// Paragraph Separator (PS) - U+2029
let d17 = yaml_parse("text: \"Section 1\\PSection 2\"\n");
print("\\nParagraph Separator (\\P):");
print(d17.text);

print("");

// ============================================================================
// PRACTICAL EXAMPLES
// ============================================================================

print("--- Practical Examples ---");

// File paths
let paths = yaml_parse("
windows: \"C:\\\\Program Files\\\\App\\\\config.ini\"
unix: \"/usr/local/bin/app\"
url: \"https://example.com/path?q=1&x=2\"
");
print("Windows path:", paths.windows);
print("Unix path:", paths.unix);
print("URL:", paths.url);

// SQL-like strings
let sql = yaml_parse("query: \"SELECT * FROM users WHERE name = 'O\\\\'Brien'\"\n");
print("\\nSQL query:", sql.query);

// JSON-like data
let json_like = yaml_parse("data: \"{\\\"name\\\": \\\"John\\\", \\\"age\\\": 30}\"\n");
print("JSON string:", json_like.data);

// Multi-line with escapes
let multi = yaml_parse("
message: \"Line 1\\n  Indented Line 2\\n    More indent\\nBack to normal\"
");
print("\\nMulti-line message:");
print(multi.message);

// ANSI color codes (for terminals)
let ansi = yaml_parse("
red: \"\\x1b[31mRed Text\\x1b[0m\"
green: \"\\x1b[32mGreen Text\\x1b[0m\"
");
print("\\nANSI codes:");
print(ansi.red);
print(ansi.green);

print("");

// ============================================================================
// COMMON MISTAKES
// ============================================================================

print("--- Common Mistakes ---");

// Single quotes don't interpret escapes
let single = yaml_parse("text: 'No\\nescape\\there'\n");
print("Single quotes (literal):", single.text);  // "No\nescape\there"

// Double quotes do interpret escapes
let double = yaml_parse("text: \"With\\nescape\"\n");
print("Double quotes (escaped):");
print(double.text);

// Invalid: Too few hex digits
// let bad1 = yaml_parse("text: \"\\xF\"\n");  // ERROR: needs 2 digits

// Invalid: Non-hex characters
// let bad2 = yaml_parse("text: \"\\xGG\"\n");  // ERROR: G not a hex digit

// Invalid: Wrong number of unicode digits
// let bad3 = yaml_parse("text: \"\\u42\"\n");  // ERROR: needs 4 digits

print("\\nRemember:");
print("- Escapes only work in DOUBLE-quoted strings (\")");
print("- Hex escapes (\\xHH) need exactly 2 hex digits");
print("- Unicode escapes need exactly 4 (\\uXXXX) or 8 (\\UXXXXXXXX) digits");
print("- Use \\\\ for literal backslash");

print("");

// ============================================================================
// COMPLETE ESCAPE REFERENCE
// ============================================================================

print("--- Complete Escape Reference ---");
print("");
print("Common Escapes:");
print("  \\\\    Backslash");
print("  \\\"    Double quote");
print("  \\/    Forward slash");
print("  \\0    Null (U+0000)");
print("  \\a    Bell (U+0007)");
print("  \\b    Backspace (U+0008)");
print("  \\t    Tab (U+0009)");
print("  \\n    Newline (U+000A)");
print("  \\v    Vertical tab (U+000B)");
print("  \\f    Form feed (U+000C)");
print("  \\r    Carriage return (U+000D)");
print("  \\e    Escape (U+001B)");
print("  \\ (space)  Space (U+0020)");
print("");
print("Hex Escape (YAML 1.2.2):");
print("  \\xHH  2-digit hex (00-FF)");
print("");
print("Unicode Escapes:");
print("  \\uXXXX      4-digit hex (0000-FFFF)");
print("  \\UXXXXXXXX  8-digit hex (00000000-0010FFFF)");
print("");
print("Special Whitespace (YAML 1.2.2):");
print("  \\N  Next line (NEL, U+0085)");
print("  \\_  Non-breaking space (NBSP, U+00A0)");
print("  \\L  Line separator (LS, U+2028)");
print("  \\P  Paragraph separator (PS, U+2029)");

print("\\n=== Demo Complete ===");
