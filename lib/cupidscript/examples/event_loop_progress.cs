// Progress indicator while background work happens

print("=== Progress Indicator Example ===\n");

event_loop_start();

// Simulate a long-running background task
async fn background_work() {
    print("Background: Starting heavy computation...");
    await sleep(2000);
    print("\nBackground: Computation complete!");
    return "Result: 42";
}

// Progress indicator task
async fn show_progress(duration_ms, interval_ms) {
    print("Main thread: Waiting for background work");
    let steps = duration_ms / interval_ms;
    for i in 1..=steps {
        print(".");
        await sleep(interval_ms);
    }
}

// Start both the background work and progress indicator concurrently
let work_promise = background_work();
let progress_promise = show_progress(2000, 100);

// Wait for both to complete using await_all
let results = await await_all([work_promise, progress_promise]);
let result = results[0];

print("\n\nMain thread: Got result: ${result}");

event_loop_stop();
