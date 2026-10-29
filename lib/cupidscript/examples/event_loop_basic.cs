// Basic event loop usage example

print("=== Basic Event Loop Example ===\n");

// Check if event loop is supported
if (!event_loop_start()) {
    print("Event loop not supported on this platform");
    print("Falling back to cooperative scheduling\n");
}

print("Event loop running: " + event_loop_running());

// Create some async work
async fn compute(name, delay, result) {
    print("${name}: starting (${delay}ms delay)");
    await sleep(delay);
    print("${name}: finished");
    return result;
}

// Start multiple operations
print("\nStarting three concurrent operations...\n");
let start = now_ms();

let p1 = compute("Task 1", 100, "result1");
let p2 = compute("Task 2", 150, "result2");
let p3 = compute("Task 3", 75, "result3");

// Wait for results
let r1 = await p1;
let r2 = await p2;
let r3 = await p3;

let elapsed = now_ms() - start;

print("\nAll tasks completed in ${elapsed}ms");
print("Results: [${r1}, ${r2}, ${r3}]");

if (event_loop_running()) {
    print("\nStopping event loop...");
    event_loop_stop();
    print("Event loop stopped");
}
