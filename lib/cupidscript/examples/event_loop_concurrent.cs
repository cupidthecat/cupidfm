// Concurrent operations with event loop

print("=== Concurrent Operations Example ===\n");

event_loop_start();

// Simulate fetching data from multiple sources
async fn fetch_data(source, delay) {
    print("Fetching from ${source}...");
    await sleep(delay);
    return {"source": source, "data": "Data from ${source}", "delay": delay};
}

// Start all fetch operations concurrently
print("Starting concurrent fetches...\n");
let start = now_ms();

let sources = [
    {"name": "API-1", "delay": 200},
    {"name": "API-2", "delay": 150},
    {"name": "API-3", "delay": 100},
    {"name": "API-4", "delay": 250},
    {"name": "API-5", "delay": 180}
];

let promises = [];
for source in sources {
    push(promises, fetch_data(source["name"], source["delay"]));
}

// Wait for all to complete
let results = await await_all(promises);
let elapsed = now_ms() - start;

print("\n=== Results ===");
for result in results {
    let source = result["source"];
    let data = result["data"];
    let delay = result["delay"];
    print("${source}: ${data} (${delay}ms)");
}

print("\nTotal time: ${elapsed}ms");
print("(Sequential would take: ${200+150+100+250+180}ms)");
print("Speedup: ${(200+150+100+250+180) / elapsed}x");

event_loop_stop();
