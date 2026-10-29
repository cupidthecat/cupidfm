// Glob Pattern Matching Demo
// Demonstrates file pattern matching with wildcards

print("=== Glob Pattern Matching Demo ===\n");

// Example 1: Find all CupidScript files
print("1. Finding all .cs files:");
let cs_files = glob("**/*.cs", "examples");
print("   Found", len(cs_files), "CupidScript files");
for file in cs_files {
    print("   -", file);
    if len(cs_files) > 10 {
        print("   ... and", len(cs_files) - 10, "more");
        break;
    }
}

// Example 2: Find all source files (C and header files)
print("\n2. Finding C source files:");
let c_files = glob("**/*.c", "src");
let h_files = glob("**/*.h", "src");
print("   C files:", len(c_files));
print("   Header files:", len(h_files));
print("   Total:", len(c_files) + len(h_files), "source files");

// Example 3: Find test files
print("\n3. Finding test files:");
let tests = glob("*.cs", "tests");
print("   Found", len(tests), "test files:");
for test in tests {
    let name = path_basename(test);
    print("   -", name);
}

// Example 4: Find markdown documentation
print("\n4. Finding documentation files:");
let docs = glob("**/*.md");
print("   Found", len(docs), "markdown files");
for doc in docs {
    if str_contains(doc, "docs") {
        print("   -", doc);
    }
}

// Example 5: Using glob for file operations
print("\n5. Processing matched files:");
let example_files = glob("*.cs", "examples");
print("   Processing", len(example_files), "example files...");
let total_size = 0;
for file in example_files {
    if exists(file) {
        let content = read_file(file);
        total_size = total_size + len(content);
    }
}
print("   Total content size:", total_size, "bytes");

// Example 6: Pattern with specific prefix
print("\n6. Finding files with specific patterns:");
let test_pattern = glob("test*.cs", "examples");
print("   Files starting with 'test':", len(test_pattern));

let demo_pattern = glob("*demo.cs", "examples");
print("   Files ending with 'demo':", len(demo_pattern));

// Example 7: Current directory glob
print("\n7. Files in current directory:");
let current = glob("*.md");
if len(current) > 0 {
    print("   Markdown files here:");
    for file in current {
        print("   -", file);
    }
} else {
    print("   No markdown files in current directory");
}

print("\n=== Glob Demo Complete ===");
