// Set Literals & Operations Examples
// Demonstrates set literal syntax and set algebra operations

println("=== CupidScript Set Features Demo ===\n");

// Example 1: Basic Set Creation
println("1. Creating Sets");
println("-".repeat(40));
let primes = #{2, 3, 5, 7, 11, 13};
let evens = #{2, 4, 6, 8, 10, 12};
let odds = #{1, 3, 5, 7, 9, 11};

println("Primes:", primes);
println("Evens:", evens);
println("Odds:", odds);
println();

// Example 2: Set Union - Combining Sets
println("2. Union (|) - Elements in either set");
println("-".repeat(40));
let all_numbers = primes | evens;
println("Primes | Evens =", all_numbers);
println();

// Example 3: Set Intersection - Common Elements
println("3. Intersection (&) - Elements in both sets");
println("-".repeat(40));
let even_primes = primes & evens;
println("Primes & Evens =", even_primes);  // Only 2
println();

// Example 4: Set Difference - Unique Elements
println("4. Difference (-) - Elements in first but not second");
println("-".repeat(40));
let odd_primes = primes - evens;
println("Primes - Evens =", odd_primes);  // All primes except 2
println();

// Example 5: Symmetric Difference - XOR
println("5. Symmetric Difference (^) - Elements in either but not both");
println("-".repeat(40));
let exclusive = primes ^ evens;
println("Primes ^ Evens =", exclusive);
println();

// Example 6: Set Methods
println("6. Set Methods");
println("-".repeat(40));
let fruits = #{"apple", "banana", "cherry"};

println("Initial fruits:", fruits);
fruits.add("date");
println("After add('date'):", fruits);

fruits.remove("banana");
println("After remove('banana'):", fruits);

println("Contains 'apple':", fruits.contains("apple"));
println("Contains 'banana':", fruits.contains("banana"));

println("Size:", fruits.size());
println();

// Example 7: Practical Use Case - Access Control
println("7. Practical Example: User Permissions");
println("-".repeat(40));
let admin_perms = #{"read", "write", "delete", "admin"};
let editor_perms = #{"read", "write", "edit"};
let viewer_perms = #{"read"};

fn check_access(user_perms, required_perms) {
    let has_access = (user_perms & required_perms).size() == required_perms.size();
    return has_access;
}

let required = #{"read", "write"};
println("Admin can read+write:", check_access(admin_perms, required));
println("Editor can read+write:", check_access(editor_perms, required));
println("Viewer can read+write:", check_access(viewer_perms, required));

let admin_only = admin_perms - editor_perms;
println("Admin-only permissions:", admin_only);
println();

// Example 8: Data Deduplication
println("8. Data Deduplication");
println("-".repeat(40));
let raw_data = [1, 2, 2, 3, 3, 3, 4, 4, 4, 4];
let unique_data = #{...raw_data};  // Spread into set to deduplicate

println("Raw data:", raw_data);
println("Unique values:", unique_data);
println("Original count:", len(raw_data));
println("Unique count:", unique_data.size());
println();

// Example 9: Set Comprehensions
println("9. Set Comprehensions");
println("-".repeat(40));
let numbers = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10];

let squares = #{x * x for x in numbers};
println("Squares:", squares);

let even_squares = #{x * x for x in numbers if x % 2 == 0};
println("Even squares:", even_squares);

// Word lengths from text
let words = ["the", "quick", "brown", "fox", "jumps"];
let word_lengths = #{len(w) for w in words};
println("Unique word lengths:", word_lengths);
println();

// Example 10: Finding Common Elements
println("10. Finding Common Interests");
println("-".repeat(40));
let alice_interests = #{"reading", "hiking", "coding", "music"};
let bob_interests = #{"coding", "gaming", "music", "cooking"};
let carol_interests = #{"reading", "music", "painting", "hiking"};

let common_alice_bob = alice_interests & bob_interests;
println("Alice & Bob common:", common_alice_bob);

let all_interests = alice_interests | bob_interests | carol_interests;
println("All unique interests:", all_interests);

let unique_to_alice = alice_interests - (bob_interests | carol_interests);
println("Unique to Alice:", unique_to_alice);
println();

// Example 11: Tag System
println("11. Tag System");
println("-".repeat(40));
let post1_tags = #{"javascript", "web", "tutorial"};
let post2_tags = #{"python", "web", "data-science"};
let post3_tags = #{"javascript", "react", "web"};

// Find posts with similar tags
fn similarity(tags1, tags2) {
    let intersection = tags1 & tags2;
    let union = tags1 | tags2;
    return intersection.size() * 100 / union.size();  // Jaccard similarity %
}

println("Post1-Post2 similarity:", similarity(post1_tags, post2_tags), "%");
println("Post1-Post3 similarity:", similarity(post1_tags, post3_tags), "%");

// All tags across posts
let all_tags = post1_tags | post2_tags | post3_tags;
println("All tags used:", all_tags);
println();

// Example 12: Mathematical Sets
println("12. Mathematical Sets");
println("-".repeat(40));
let natural = #{1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
let even_natural = #{2, 4, 6, 8, 10};
let odd_natural = #{1, 3, 5, 7, 9};
let squares_natural = #{1, 4, 9};

println("Even ∪ Odd = Natural?", (even_natural | odd_natural) == natural);  // Should be false (== not implemented for sets)
println("Even ∩ Odd = ∅?", (even_natural & odd_natural).size() == 0);
println("Squares ⊆ Natural?", (squares_natural - natural).size() == 0);
println();

println("=== Demo Complete ===");
