// Batch processing with concurrent workers

print("=== Batch Processing Example ===\n");

event_loop_start();

// Process an item (simulated work)
async fn process_item(id) {
    let delay = 50 + (id * 10);  // Variable processing time
    await sleep(delay);
    return id * id;
}

// Process items in batches
fn process_batch(items, batch_size) {
    let results = [];
    let total_items = len(items);
    let processed = 0;

    print("Processing ${total_items} items in batches of ${batch_size}...\n");

    while (processed < total_items) {
        // Get next batch
        let batch_promises = [];
        let batch_end = processed + batch_size;
        if (batch_end > total_items) {
            batch_end = total_items;
        }

        // Start batch processing
        for i in processed..batch_end {
            push(batch_promises, process_item(items[i]));
        }

        let batch_num = processed / batch_size + 1;
        let batch_start = processed;
        let batch_end_idx = batch_end - 1;
        print("Batch ${batch_num}: Processing items ${batch_start} to ${batch_end_idx}...");

        // Wait for batch to complete
        let batch_results = await await_all(batch_promises);

        // Add to results
        for result in batch_results {
            push(results, result);
        }

        processed = batch_end;
        print("Batch complete! (${len(results)} items done)\n");
    }

    return results;
}

// Process 20 items in batches of 5
let items = [];
for i in 1..=20 {
    push(items, i);
}

let start = now_ms();
let results = process_batch(items, 5);
let elapsed = now_ms() - start;

print("\n=== Results ===");
print("Processed ${len(results)} items in ${elapsed}ms");
print("First 10 results: [" + results[0] + ", " + results[1] + ", " + results[2] + ", " + results[3] + ", " + results[4] + ", " + results[5] + ", " + results[6] + ", " + results[7] + ", " + results[8] + ", " + results[9] + "]");

event_loop_stop();
