// Test that sleep works in background event loop
event_loop_start();

let start = now_ms();
let p1 = sleep(100);
let p2 = sleep(200);

// Main thread continues immediately
let mid = now_ms();
let elapsed_mid = mid - start;
assert(elapsed_mid < 50, "main thread should not block on sleep");

// Wait for both promises
await p1;
let after_p1 = now_ms();
let elapsed_p1 = after_p1 - start;
assert(elapsed_p1 >= 100, "p1 should take at least 100ms");
assert(elapsed_p1 < 150, "p1 should take less than 150ms");

await p2;
let after_p2 = now_ms();
let elapsed_p2 = after_p2 - start;
assert(elapsed_p2 >= 200, "p2 should take at least 200ms");
assert(elapsed_p2 < 250, "p2 should take less than 250ms");

event_loop_stop();

print("event loop background sleep ok");
