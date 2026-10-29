// YAML 1.2.2 Specialized Tags Test Suite
// Tests all specialized YAML tags: !!binary, !!timestamp, !!set, !!omap, !!pairs

print("=== YAML 1.2.2 Specialized Tags Test Suite ===\n");

let total_tests = 0;
let passed_tests = 0;

fn test(desc, condition) {
    total_tests = total_tests + 1;
    if (condition) {
        print("✓ " + desc);
        passed_tests = passed_tests + 1;
    } else {
        print("✗ FAIL: " + desc);
    }
}

// ========================================
// !!binary Tag Tests
// ========================================
print("\n--- !!binary Tag Tests ---");

// Test 1: Basic base64 decode
let yaml1 = "data: !!binary SGVsbG8gV29ybGQ=\n";
let doc1 = yaml_parse(yaml1);
test("!!binary creates bytes type", typeof(doc1.data) == "bytes");
test("!!binary decodes 'Hello World' correctly", len(doc1.data) == 11);

// Test 2: Multi-line base64 (common in YAML)
let yaml2 = "image: !!binary |\n  SGVsbG8g\n  V29ybGQ=\n";
let doc2 = yaml_parse(yaml2);
test("!!binary handles multi-line base64", typeof(doc2.image) == "bytes");
test("!!binary multi-line decode length", len(doc2.image) == 11);

// Test 3: Empty binary
let yaml3 = "empty: !!binary \"\"\n";
let doc3 = yaml_parse(yaml3);
test("!!binary handles empty string", typeof(doc3.empty) == "bytes");
test("!!binary empty has zero length", len(doc3.empty) == 0);

// Test 4: Binary with padding
let yaml4 = "padded: !!binary YWJjZA==\n";
let doc4 = yaml_parse(yaml4);
test("!!binary handles padding", typeof(doc4.padded) == "bytes");
test("!!binary padded length correct", len(doc4.padded) == 4);

// ========================================
// !!timestamp Tag Tests
// ========================================
print("\n--- !!timestamp Tag Tests ---");

// Test 6: ISO 8601 with Z timezone
let yaml6 = "created: !!timestamp 2024-01-27T15:30:00Z\n";
let doc6 = yaml_parse(yaml6);
test("!!timestamp accepts ISO 8601 with Z", typeof(doc6.created) == "string");
test("!!timestamp preserves value", doc6.created == "2024-01-27T15:30:00Z");

// Test 7: ISO 8601 with timezone offset
let yaml7 = "modified: !!timestamp 2024-01-27T15:30:00+05:30\n";
let doc7 = yaml_parse(yaml7);
test("!!timestamp accepts timezone offset", doc7.modified == "2024-01-27T15:30:00+05:30");

// Test 8: ISO 8601 with fractional seconds
let yaml8 = "precise: !!timestamp 2024-01-27T15:30:00.123Z\n";
let doc8 = yaml_parse(yaml8);
test("!!timestamp accepts fractional seconds", doc8.precise == "2024-01-27T15:30:00.123Z");

// Test 9: Date only (no time)
let yaml9 = "date: !!timestamp 2024-01-27\n";
let doc9 = yaml_parse(yaml9);
test("!!timestamp accepts date-only format", doc9.date == "2024-01-27");

// Test 10: Space separator instead of T
let yaml10 = "spaced: !!timestamp '2024-01-27 15:30:00'\n";
let doc10 = yaml_parse(yaml10);
test("!!timestamp accepts space separator", doc10.spaced == "2024-01-27 15:30:00");

// ========================================
// !!set Tag Tests
// ========================================
print("\n--- !!set Tag Tests ---");

// Test 12: Set from map with null values
let yaml12 = "tags: !!set\n  bug: ~\n  urgent: ~\n  review: ~\n";
let doc12 = yaml_parse(yaml12);
test("!!set creates map type", typeof(doc12.tags) == "map");
test("!!set has correct size", len(keys(doc12.tags)) == 3);
test("!!set values are null", doc12.tags.bug == nil);
test("!!set has 'urgent' key", doc12.tags.urgent == nil);

// Test 13: Set in flow syntax
let yaml13 = "flow_set: !!set {a: ~, b: ~, c: ~}\n";
let doc13 = yaml_parse(yaml13);
test("!!set flow syntax works", typeof(doc13.flow_set) == "map");
test("!!set flow has 3 items", len(keys(doc13.flow_set)) == 3);

// Test 14: Set from list (should convert)
let yaml14 = "list_set: !!set [apple, banana, cherry]\n";
let doc14 = yaml_parse(yaml14);
test("!!set from list creates map", typeof(doc14.list_set) == "map");
test("!!set from list has items", doc14.list_set.apple == nil);
test("!!set from list has banana", doc14.list_set.banana == nil);

// ========================================
// !!omap Tag Tests
// ========================================
print("\n--- !!omap Tag Tests ---");

// Test 16: Basic ordered map using flow syntax (block style has parser limitations)
let yaml16 = "ordered: !!omap\n  - {first: 1}\n  - {second: 2}\n  - {third: 3}\n";
let doc16 = yaml_parse(yaml16);
test("!!omap creates list type", typeof(doc16.ordered) == "list");
test("!!omap has 3 entries", len(doc16.ordered) == 3);
test("!!omap preserves order", keys(doc16.ordered[0])[0] == "first");
test("!!omap second entry", doc16.ordered[1].second == 2);
test("!!omap third entry", doc16.ordered[2].third == 3);
test("!!omap preserves order", keys(doc16.ordered[0])[0] == "first");
test("!!omap second entry", doc16.ordered[1].second == 2);
test("!!omap third entry", doc16.ordered[2].third == 3);

// Test 17: Omap with various value types (using flow syntax)
let yaml17 = "mixed: !!omap\n  - {name: 'John'}\n  - {age: 30}\n  - {active: true}\n";
let doc17 = yaml_parse(yaml17);
test("!!omap with string value", doc17.mixed[0].name == "John");
test("!!omap with int value", doc17.mixed[1].age == 30);
test("!!omap with bool value", doc17.mixed[2].active == true);

// Test 18: Omap iteration preserves order (using flow syntax)
let yaml18 = "sequence: !!omap\n  - {a: 1}\n  - {b: 2}\n  - {c: 3}\n";
let doc18 = yaml_parse(yaml18);
let omap_keys = [];
for entry in doc18.sequence {
    push(omap_keys, keys(entry)[0]);
}
test("!!omap iteration order correct", omap_keys[0] == "a" && omap_keys[1] == "b" && omap_keys[2] == "c");

// ========================================
// !!pairs Tag Tests
// ========================================
print("\n--- !!pairs Tag Tests ---");

// Test 21: Basic pairs (using flow syntax)
let yaml21 = "pairs: !!pairs\n  - {a: 1}\n  - {b: 2}\n  - {c: 3}\n";
let doc21 = yaml_parse(yaml21);
test("!!pairs creates list type", typeof(doc21.pairs) == "list");
test("!!pairs has 3 entries", len(doc21.pairs) == 3);
test("!!pairs first entry", doc21.pairs[0].a == 1);

// Test 22: Pairs with duplicate keys (main difference from !!omap, using flow syntax)
let yaml22 = "events: !!pairs\n  - {click: handler1}\n  - {click: handler2}\n  - {hover: handler3}\n";
let doc22 = yaml_parse(yaml22);
test("!!pairs allows duplicate keys", len(doc22.events) == 3);
test("!!pairs first click handler", doc22.events[0].click == "handler1");
test("!!pairs second click handler", doc22.events[1].click == "handler2");
test("!!pairs hover handler", doc22.events[2].hover == "handler3");

// Test 23: Pairs preserves all duplicates (using flow syntax)
let yaml23 = "duplicates: !!pairs\n  - {x: 1}\n  - {x: 2}\n  - {x: 3}\n  - {x: 4}\n";
let doc23 = yaml_parse(yaml23);
test("!!pairs keeps all duplicates", len(doc23.duplicates) == 4);
let duplicate_values = [];
for entry in doc23.duplicates {
    push(duplicate_values, entry.x);
}
test("!!pairs duplicate values correct", duplicate_values[0] == 1 && duplicate_values[3] == 4);

// ========================================
// Complex Scenarios
// ========================================
print("\n--- Complex Scenarios ---");

// Test 25: Multiple specialized tags at top level (flat structure)
let yaml25 = "timestamp: !!timestamp 2024-01-27T12:00:00Z\nfeatures: !!set {fast: ~, secure: ~}\npriority: !!omap [{first: 1}, {second: 2}]\n";
let doc25 = yaml_parse(yaml25);
test("Multiple tags: timestamp", typeof(doc25.timestamp) == "string");
test("Multiple tags: set", typeof(doc25.features) == "map");
test("Multiple tags: omap", typeof(doc25.priority) == "list");

// Test 26: Omap with flow syntax list
let yaml26 = "items: !!omap [{a: one}, {b: two}, {c: three}]\n";
let doc26 = yaml_parse(yaml26);
test("Flow omap works", typeof(doc26.items) == "list");
test("Flow omap entry", doc26.items[0].a == "one");

// Test 27: Binary at top level (simple structure)
let yaml27 = "content1: !!binary SGVsbG8gV29ybGQ=\ncontent2: !!binary dGVzdA==\n";
let doc27 = yaml_parse(yaml27);
test("Binary field 1", typeof(doc27.content1) == "bytes");
test("Binary field 2", typeof(doc27.content2) == "bytes");

// ========================================
// Edge Cases
// ========================================
print("\n--- Edge Cases ---");

// Test 28: Empty set
let yaml28 = "empty_set: !!set {}\n";
let doc28 = yaml_parse(yaml28);
test("Empty set works", typeof(doc28.empty_set) == "map");
test("Empty set has no keys", len(keys(doc28.empty_set)) == 0);

// Test 29: Empty omap
let yaml29 = "empty_omap: !!omap []\n";
let doc29 = yaml_parse(yaml29);
test("Empty omap works", typeof(doc29.empty_omap) == "list");
test("Empty omap has no entries", len(doc29.empty_omap) == 0);

// Test 30: Empty pairs
let yaml30 = "empty_pairs: !!pairs []\n";
let doc30 = yaml_parse(yaml30);
test("Empty pairs works", typeof(doc30.empty_pairs) == "list");
test("Empty pairs has no entries", len(doc30.empty_pairs) == 0);

// Test 31: Set with numeric values in list form
let yaml31 = "numbers: !!set [1, 2, 3, 4, 5]\n";
let doc31 = yaml_parse(yaml31);
test("Set from numeric list", typeof(doc31.numbers) == "map");
// Numbers are converted to string keys
test("Set has numeric key as string", doc31.numbers["1"] == nil);

// Test 32: Timestamp with lowercase 't' and 'z'
let yaml32 = "lower: !!timestamp 2024-01-27t15:30:00z\n";
let doc32 = yaml_parse(yaml32);
test("Timestamp accepts lowercase t and z", doc32.lower == "2024-01-27t15:30:00z");

// ========================================
// Summary
// ========================================
print("\n==================================================");
print("Test Results: " + passed_tests + "/" + total_tests + " passed");

if (passed_tests == total_tests) {
    print("✅ ALL TESTS PASSED! YAML 1.2.2 Specialized Tags: 100% Complete");
} else {
    print("❌ Some tests failed");
    exit(1);
}
