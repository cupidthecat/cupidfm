// GC Auto-Collect Policy Demo

print("=== GC Auto-Collect Demo ===\n");

// Check initial configuration
let config = gc_config();
print("Initial config:");
print("  Threshold:", config.threshold);
print("  Alloc trigger:", config.alloc_trigger);

// Check initial stats
let stats = gc_stats();
print("\nInitial stats:");
print("  Tracked objects:", stats.tracked);
print("  Collections:", stats.collections);
print("  Collected:", stats.collected);
print("  Allocations:", stats.allocations);

// Configure auto-GC
print("\n=== Configuring Auto-GC ===");
print("Setting threshold=50, alloc_trigger=25");
gc_config(50, 25);

config = gc_config();
print("New config:");
print("  Threshold:", config.threshold);
print("  Alloc trigger:", config.alloc_trigger);

// Create many objects to trigger GC
print("\n=== Creating objects ===");
let objects = [];
let i = 0;
for (i = 0; i < 100; i = i + 1) {
    let obj = {"id": i, "data": [1, 2, 3]};
    push(objects, obj);
    
    // Show stats every 20 allocations
    if (i % 20 == 19) {
        stats = gc_stats();
        print("After", i + 1, "objects:");
        print("  Tracked:", stats.tracked, "Collections:", stats.collections);
    }
}

// Final stats
print("\n=== Final Stats ===");
stats = gc_stats();
print("Tracked objects:", stats.tracked);
print("Total collections:", stats.collections);
print("Objects collected:", stats.collected);
print("Total allocations:", stats.allocations);

// Create a cycle and demonstrate collection
print("\n=== Cycle Detection ===");
let a = [1, 2, 3];
let b = [4, 5, 6];
a[0] = b;
b[0] = a;  // Create cycle

print("Created cycle between two lists");
stats = gc_stats();
print("Tracked before:", stats.tracked);

// Clear references
a = nil;
b = nil;

// Manual GC to collect cycle
let collected = gc();
print("Manual gc() collected:", collected, "cycles");

stats = gc_stats();
print("Tracked after:", stats.tracked);

// Disable auto-GC
print("\n=== Disabling Auto-GC ===");
gc_config(0, 0);
config = gc_config();
print("Auto-GC disabled:");
print("  Threshold:", config.threshold);
print("  Alloc trigger:", config.alloc_trigger);

print("\n=== Demo Complete ===");
