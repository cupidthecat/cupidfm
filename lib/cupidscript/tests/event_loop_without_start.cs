// Test async/await without starting event loop (cooperative scheduling fallback)
assert(!event_loop_running(), "loop should not be running");

async fn add(a, b) {
    return a + b;
}

let result = await add(3, 7);
assert(result == 10, "async function should work without event loop");

// Sleep should also work (cooperative)
let start = now_ms();
await sleep(50);
let elapsed = now_ms() - start;
assert(elapsed >= 50, "sleep should work without event loop");

print("event loop without start ok");
