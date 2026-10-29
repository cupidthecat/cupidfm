// Test basic event loop start/stop
assert(!event_loop_running(), "loop should not be running initially");

let started = event_loop_start();
assert(started, "event_loop_start should return true");
assert(event_loop_running(), "loop should be running after start");

// Try starting again - should fail
let started_again = event_loop_start();
assert(!started_again, "event_loop_start should return false when already running");

let stopped = event_loop_stop();
assert(stopped, "event_loop_stop should return true");
assert(!event_loop_running(), "loop should not be running after stop");

// Stop again - should be idempotent
let stopped_again = event_loop_stop();
assert(stopped_again, "event_loop_stop should return true even when not running");

print("event loop basic ok");
