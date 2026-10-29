// Helper function for managing event loop lifecycle

// Wrapper that automatically manages event loop
fn with_event_loop(callback) {
    let started = event_loop_start();

    if (!started) {
        print("Warning: Event loop not available, using cooperative scheduling");
    }

    try {
        callback();
    } catch (e) {
        if (started) {
            event_loop_stop();
        }
        throw e;
    }

    if (started) {
        event_loop_stop();
    }
}

// Usage example
print("=== Event Loop Helper Example ===\n");

with_event_loop(fn() {
    print("Event loop is running: " + event_loop_running());

    async fn work(id) {
        await sleep(100);
        return "Task ${id} done";
    }

    let p1 = work(1);
    let p2 = work(2);
    let p3 = work(3);

    print(await p1);
    print(await p2);
    print(await p3);

    // Event loop will be automatically stopped
});

print("\nEvent loop is running: " + event_loop_running());
print("Cleanup handled automatically!");
