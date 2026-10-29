// Test concurrent async tasks in event loop
event_loop_start();

let counter = 0;

async fn increment(delay_ms, amount) {
    await sleep(delay_ms);
    return amount;
}

// Start multiple tasks
let p1 = increment(50, 10);
let p2 = increment(100, 20);
let p3 = increment(75, 15);

// All should run concurrently
let start = now_ms();

let r1 = await p1;
let r2 = await p2;
let r3 = await p3;

let elapsed = now_ms() - start;

assert(r1 == 10, "p1 should return 10");
assert(r2 == 20, "p2 should return 20");
assert(r3 == 15, "p3 should return 15");

// Should take ~100ms total (longest task), not 225ms (sum)
assert(elapsed >= 100, "tasks should take at least 100ms");
assert(elapsed < 150, "tasks should run concurrently, not sequentially");

event_loop_stop();

print("event loop concurrent tasks ok");
