// Set Comprehension Examples
// Demonstrating Python-style set comprehension syntax in CupidScript

print("=== Set Comprehension Demo ===\n");

// Example 1: Get unique word lengths
print("Example 1: Unique word lengths");
let words = ["hello", "world", "hi", "hey", "greetings"];
let lengths = {len(w) for w in words};
print("Words:", words);
print("Unique lengths:", lengths);
print();

// Example 2: Extract unique characters from text
print("Example 2: Unique characters");
let text = "hello world";
let unique_chars = {c for c in text if c != " "};
print("Text:", text);
print("Unique characters:", unique_chars);
print();

// Example 3: Find unique remainders
print("Example 3: Modulo classes");
let numbers = range(20);
let remainders = {n % 5 for n in numbers};
print("Numbers:", list(numbers));
print("Unique remainders (mod 5):", remainders);
print();

// Example 4: Unique first letters
print("Example 4: First letters");
let names = ["Alice", "Bob", "Andrew", "Bella", "Charlie", "Amy"];
let first_letters = {substr(name, 0, 1) for name in names};
print("Names:", names);
print("Unique first letters:", first_letters);
print();

// Example 5: Remove duplicates while transforming
print("Example 5: Unique uppercase names");
let mixed_case = ["alice", "ALICE", "Bob", "bob", "Charlie"];
let uppercase_unique = {upper(name) for name in mixed_case};
print("Mixed case:", mixed_case);
print("Unique uppercase:", uppercase_unique);
print();

// Example 6: Extract unique values from nested data
print("Example 6: Unique values from map");
let data = {
    user1: {age: 25, city: "NYC"},
    user2: {age: 30, city: "LA"},
    user3: {age: 25, city: "NYC"}
};
let unique_ages = {v.age for k, v in data};
let unique_cities = {v.city for k, v in data};
print("Users:", data);
print("Unique ages:", unique_ages);
print("Unique cities:", unique_cities);
print();

// Example 7: Filtered set comprehension
print("Example 7: Even squares");
let even_squares = {x * x for x in range(10) if x % 2 == 0};
print("Even squares from 0-9:", even_squares);
print();

// Example 8: Set operations with comprehensions
print("Example 8: Vowels in sentence");
let sentence = "The quick brown fox jumps";
let vowels = set(["a", "e", "i", "o", "u", "A", "E", "I", "O", "U"]);
let found_vowels = {c for c in sentence if set_has(vowels, c)};
print("Sentence:", sentence);
print("Vowels found:", found_vowels);
print();

// Example 9: Unique tags from items
print("Example 9: Collect all tags");
let items = [
    {name: "item1", tags: ["red", "large"]},
    {name: "item2", tags: ["blue", "small"]},
    {name: "item3", tags: ["red", "small"]}
];
// Flatten tags using nested iteration (when available)
let all_tags = set([]);
for item in items {
    for tag in item.tags {
        set_add(all_tags, tag);
    }
}
print("Items:", items);
print("All unique tags:", all_tags);
print();

// Example 10: Comparison - list vs set comprehension
print("Example 10: List vs Set comprehension");
let list_with_dups = [x % 3 for x in range(10)];
let set_no_dups = {x % 3 for x in range(10)};
print("List comprehension (allows duplicates):", list_with_dups);
print("Set comprehension (unique only):", set_no_dups);
print("List length:", len(list_with_dups));
print("Set length:", len(set_no_dups));

print("\n✨ Set comprehensions make it easy to collect unique values!");
