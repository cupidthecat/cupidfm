// Test await_all with event loop
event_loop_start();

async fn compute(val) {
    await sleep(50);
    return val * 2;
}

let promises = [];
for i in 1..=5 {
    push(promises, compute(i));
}

let start = now_ms();
let results = await await_all(promises);
let elapsed = now_ms() - start;

assert(len(results) == 5, "should have 5 results");
assert(results[0] == 2, "results[0] should be 2");
assert(results[1] == 4, "results[1] should be 4");
assert(results[2] == 6, "results[2] should be 6");
assert(results[3] == 8, "results[3] should be 8");
assert(results[4] == 10, "results[4] should be 10");

// Should take ~50ms (concurrent), not 250ms (sequential)
assert(elapsed >= 50, "should take at least 50ms");
assert(elapsed < 100, "all promises should resolve concurrently");

event_loop_stop();

print("event loop await_all ok");
