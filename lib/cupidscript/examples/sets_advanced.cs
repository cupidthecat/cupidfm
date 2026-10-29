// Advanced Set Operations Examples
// Real-world use cases for set literals and operations

println("=== Advanced Set Usage Examples ===\n");

// Example 1: Recipe Ingredient Checker
println("1. Recipe Ingredient Checker");
println("-".repeat(50));

let pantry = #{"flour", "sugar", "eggs", "butter", "milk", "salt"};
let recipe_cookies = #{"flour", "sugar", "eggs", "butter"};
let recipe_cake = #{"flour", "sugar", "eggs", "butter", "milk", "baking_powder"};

fn can_make_recipe(pantry, recipe) {
    let missing = recipe - pantry;
    if (missing.size() == 0) {
        return {can_make: true, missing: #{}};
    }
    return {can_make: false, missing: missing};
}

let cookies_check = can_make_recipe(pantry, recipe_cookies);
println("Can make cookies:", cookies_check.can_make);

let cake_check = can_make_recipe(pantry, recipe_cake);
println("Can make cake:", cake_check.can_make);
println("Missing for cake:", cake_check.missing);
println();

// Example 2: Social Network - Friend Recommendations
println("2. Friend Recommendations");
println("-".repeat(50));

let alice_friends = #{"bob", "charlie", "david"};
let bob_friends = #{"alice", "charlie", "eve", "frank"};
let charlie_friends = #{"alice", "bob", "david", "grace"};

fn recommend_friends(user_friends, other_friends) {
    // Recommend friends of friends who aren't already friends
    // Exclude the user themselves
    return other_friends - user_friends;
}

let alice_recommendations = recommend_friends(alice_friends, bob_friends);
println("Alice might know:", alice_recommendations);

// Find mutual friends
let mutual = alice_friends & bob_friends;
println("Alice and Bob's mutual friends:", mutual);
println();

// Example 3: Skill Matching for Jobs
println("3. Job Skill Matching");
println("-".repeat(50));

let job_requirements = #{
    "python", "javascript", "sql", "docker", "aws"
};

let candidate1_skills = #{
    "python", "javascript", "sql", "git"
};

let candidate2_skills = #{
    "python", "javascript", "sql", "docker", "aws", "kubernetes"
};

fn evaluate_candidate(candidate_skills, job_skills) {
    let matched = candidate_skills & job_skills;
    let missing = job_skills - candidate_skills;
    let extra = candidate_skills - job_skills;
    let match_percent = matched.size() * 100 / job_skills.size();
    
    return {
        matched: matched,
        missing: missing,
        extra: extra,
        score: match_percent
    };
}

let eval1 = evaluate_candidate(candidate1_skills, job_requirements);
println("Candidate 1:");
println("  Matched skills:", eval1.matched);
println("  Missing skills:", eval1.missing);
println("  Match score:", eval1.score, "%");

let eval2 = evaluate_candidate(candidate2_skills, job_requirements);
println("\nCandidate 2:");
println("  Matched skills:", eval2.matched);
println("  Missing skills:", eval2.missing);
println("  Extra skills:", eval2.extra);
println("  Match score:", eval2.score, "%");
println();

// Example 4: Access Control Lists (ACL)
println("4. Access Control System");
println("-".repeat(50));

let file_owners = #{"alice", "bob"};
let file_editors = #{"alice", "bob", "charlie"};
let file_viewers = #{"alice", "bob", "charlie", "david", "eve"};

fn check_permission(user, level) {
    if (level == "owner") {
        return file_owners.contains(user);
    } else if (level == "edit") {
        return file_editors.contains(user);
    } else if (level == "view") {
        return file_viewers.contains(user);
    }
    return false;
}

println("Alice can edit:", check_permission("alice", "edit"));
println("Charlie can edit:", check_permission("charlie", "edit"));
println("David can view:", check_permission("david", "view"));
println("Eve can edit:", check_permission("eve", "edit"));

// Audit: who has elevated permissions?
let elevated = file_editors - file_viewers;
println("Users with edit but not view:", elevated);  // Should be empty
println();

// Example 5: Content Filtering
println("5. Content Filtering");
println("-".repeat(50));

let blocked_words = #{"spam", "scam", "virus", "malware"};
let flagged_words = #{"free", "click", "winner", "prize"};

fn check_content(text) {
    let words = #{w.lower() for w in text.split(" ")};
    let has_blocked = (words & blocked_words).size() > 0;
    let has_flagged = (words & flagged_words).size() > 0;
    
    return {
        safe: !has_blocked,
        flagged: has_flagged,
        blocked_found: words & blocked_words,
        flagged_found: words & flagged_words
    };
}

let email1 = "You won a free prize!";
let result1 = check_content(email1);
println("Email:", email1);
println("  Safe:", result1.safe);
println("  Flagged:", result1.flagged);
println("  Flagged words:", result1.flagged_found);

let email2 = "This is spam with a virus!";
let result2 = check_content(email2);
println("\nEmail:", email2);
println("  Safe:", result2.safe);
println("  Blocked words:", result2.blocked_found);
println();

// Example 6: Version Control - File Changes
println("6. Version Control Diff");
println("-".repeat(50));

let commit1_files = #{"main.cs", "utils.cs", "README.md"};
let commit2_files = #{"main.cs", "utils.cs", "config.cs", "README.md"};
let commit3_files = #{"main.cs", "config.cs", "README.md", "test.cs"};

fn diff_commits(old_files, new_files) {
    return {
        added: new_files - old_files,
        removed: old_files - new_files,
        unchanged: old_files & new_files,
        modified: old_files & new_files  // Would need timestamps in real scenario
    };
}

let diff1_2 = diff_commits(commit1_files, commit2_files);
println("Commit1 → Commit2:");
println("  Added:", diff1_2.added);
println("  Removed:", diff1_2.removed);

let diff2_3 = diff_commits(commit2_files, commit3_files);
println("\nCommit2 → Commit3:");
println("  Added:", diff2_3.added);
println("  Removed:", diff2_3.removed);
println();

// Example 7: Database Query Optimization
println("7. Query Filter Optimization");
println("-".repeat(50));

let index_columns = #{"user_id", "created_at", "status"};
let query_filters = #{"user_id", "status", "region"};

let indexed_filters = query_filters & index_columns;
let unindexed_filters = query_filters - index_columns;

println("Query uses indexed columns:", indexed_filters);
println("Query uses unindexed columns:", unindexed_filters);

if (unindexed_filters.size() > 0) {
    println("⚠️  Warning: Query may be slow due to unindexed filters");
    println("   Consider adding indexes for:", unindexed_filters);
}
println();

// Example 8: Set-based Routing
println("8. Feature Flag Routing");
println("-".repeat(50));

let beta_users = #{"alice", "bob"};
let premium_users = #{"charlie", "david", "alice"};
let blocked_users = #{"eve"};

fn get_features(user) {
    let features = #{"basic"};
    
    if (beta_users.contains(user)) {
        features.add("beta");
    }
    
    if (premium_users.contains(user)) {
        features.add("premium");
    }
    
    if (blocked_users.contains(user)) {
        features.clear();
        features.add("limited");
    }
    
    return features;
}

println("Alice's features:", get_features("alice"));
println("Charlie's features:", get_features("charlie"));
println("Eve's features:", get_features("eve"));
println("Frank's features:", get_features("frank"));
println();

println("=== Advanced Examples Complete ===");
