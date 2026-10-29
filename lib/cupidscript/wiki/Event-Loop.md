# Event Loop

CupidScript provides a background event loop for true asynchronous execution. When started, the event loop runs in a separate thread, allowing async operations to progress while your main code continues executing.

## Platform Support

| Platform | Support | Implementation | Notes |
|----------|---------|----------------|-------|
| Linux | ✅ Full | poll() + pthread | Background thread with poll-based I/O multiplexing |
| macOS | ✅ Full | poll() + pthread | Background thread with poll-based I/O multiplexing |
| Unix | ✅ Full | poll() + pthread | Background thread with poll-based I/O multiplexing |
| Windows | ⚠️ Partial | select() | Uses select() instead of poll(); functions return false for background loop |

**Implementation Details:**
- **Unix/Linux/macOS**: Uses `poll()` system call for efficient I/O multiplexing
- **Windows**: Uses `select()` for I/O multiplexing (poll not available)
- **Background thread**: Only available on POSIX platforms (pthread)
- **Cooperative fallback**: All platforms support cooperative scheduling without event loop

The event loop is detected at compile time:
- `CS_USE_POLL` defined on POSIX systems
- `CS_USE_SELECT` defined on Windows

## Basic Usage

```cs
// Start the event loop
event_loop_start();

// Schedule async work
let p1 = sleep(1000);
let p2 = sleep(2000);

// Main thread continues immediately
print("Working while timers run...");

// Wait for results when needed
await p1;
print("First timer done");
await p2;
print("Second timer done");

// Stop the event loop
event_loop_stop();
```

## API Functions

### `event_loop_start() -> bool`

Starts the background event loop thread.

**Returns:**
- `true` on success
- `false` if already running or on unsupported platforms

**Example:**
```cs
if (event_loop_start()) {
    print("Event loop started");
} else {
    print("Failed to start event loop");
}
```

**Notes:**
- Safe to call multiple times (idempotent on failure)
- On Windows, returns `false` and uses cooperative scheduling instead
- The event loop processes tasks, timers, and I/O operations in the background

### `event_loop_stop() -> bool`

Stops the background event loop thread.

**Returns:**
- `true` on success

**Example:**
```cs
event_loop_stop();
print("Event loop stopped");
```

**Notes:**
- Safe to call multiple times (idempotent)
- Waits for the background thread to exit before returning
- Automatically called when the VM is destroyed

### `event_loop_running() -> bool`

Checks if the event loop is currently running.

**Returns:**
- `true` if the background loop is active
- `false` otherwise

**Example:**
```cs
if (event_loop_running()) {
    print("Event loop is active");
} else {
    print("Event loop is not running");
}
```

## Common Patterns

### Concurrent Operations

The event loop allows multiple async operations to progress simultaneously:

```cs
event_loop_start();

async fn fetch_data(id) {
    await sleep(100);  // Simulate network delay
    return {"id": id, "data": "result"};
}

// Start multiple operations concurrently
let p1 = fetch_data(1);
let p2 = fetch_data(2);
let p3 = fetch_data(3);

// All run in parallel - takes ~100ms total, not 300ms
let r1 = await p1;
let r2 = await p2;
let r3 = await p3;

event_loop_stop();
```

### Non-Blocking Timers

With the event loop, timers don't block the main thread:

```cs
event_loop_start();

let long_timer = sleep(5000);  // 5 second timer

// Main thread continues immediately
for i in 1..=10 {
    print("Tick " + i);
    await sleep(100);  // Small delay
}

// Now wait for the long timer if needed
await long_timer;

event_loop_stop();
```

### Batch Processing

Process multiple items concurrently:

```cs
event_loop_start();

async fn process_item(item) {
    await sleep(item * 10);  // Simulated work
    return item * 2;
}

let items = [1, 2, 3, 4, 5];
let promises = [];

for item in items {
    push(promises, process_item(item));
}

// Wait for all to complete
let results = await await_all(promises);
print("Results: " + results);

event_loop_stop();
```

## Cooperative Scheduling Fallback

If the event loop is not started or on unsupported platforms, async operations still work using cooperative scheduling:

```cs
// No event_loop_start() call

async fn compute(x) {
    await sleep(100);
    return x * 2;
}

// This works, but runs synchronously
let result = await compute(5);  // Blocks for 100ms
print(result);  // 10
```

The difference:
- **With event loop:** Multiple operations progress concurrently
- **Without event loop:** Operations run one at a time when awaited

## Best Practices

### 1. Start Early, Stop Late

```cs
// Start at the beginning of async work
event_loop_start();

// ... do async work ...

// Stop at the end
event_loop_stop();
```

### 2. Use `await_all` for Concurrent Operations

```cs
event_loop_start();

let promises = [op1(), op2(), op3()];
let results = await await_all(promises);  // All run concurrently

event_loop_stop();
```

### 3. Don't Forget to Stop

The event loop runs in a background thread. Always stop it when done:

```cs
event_loop_start();

try {
    // ... async work ...
} finally {
    event_loop_stop();  // Ensures cleanup
}
```

Or use a wrapper:

```cs
fn with_event_loop(callback) {
    event_loop_start();
    try {
        callback();
    } finally {
        event_loop_stop();
    }
}

with_event_loop(fn() {
    // Your async code here
});
```

### 4. Check Platform Support

```cs
if (!event_loop_start()) {
    print("Warning: Event loop not supported, using cooperative scheduling");
}
```

## Performance Considerations

### Thread Overhead

The background event loop uses a single thread that:
- Sleeps when no work is available (1ms intervals)
- Wakes up when timers are due or I/O is ready
- Broadcasts to waiting threads when promises resolve

### When to Use the Event Loop

**Use the event loop when:**
- You have multiple concurrent async operations
- You want non-blocking timers or I/O
- Your main thread needs to continue working while async operations progress

**Don't use the event loop when:**
- You only have sequential async operations
- You're running on Windows and need guarantees
- The overhead of a background thread isn't worth it

## Thread Safety

The event loop is designed to be thread-safe:
- Promise resolution uses mutex protection
- Condition variables notify waiting threads
- Multiple threads can `await` promises safely

**However:**
- User code is not automatically thread-safe
- Only the VM's internal structures are protected
- Avoid sharing mutable data between async tasks without synchronization

## Relationship with Async/Await

The event loop enhances async/await but doesn't replace it:

- **`async fn` and `await`**: Always work, with or without event loop
- **Event loop**: Enables true concurrency for async operations

See [Async-Await](Async-Await.md) for more details on async functions.

## Troubleshooting

### Event Loop Doesn't Start

```cs
if (!event_loop_start()) {
    print("Event loop failed to start");
    // Check: Are you on Windows?
    // Check: Did you already start it?
}
```

### Operations Don't Run Concurrently

Make sure:
1. Event loop is started: `event_loop_running()` returns `true`
2. You're starting operations before awaiting them:
   ```cs
   // Good - concurrent
   let p1 = async_op();
   let p2 = async_op();
   await p1;
   await p2;

   // Bad - sequential
   let r1 = await async_op();
   let r2 = await async_op();
   ```

### Program Hangs on Exit

Make sure to call `event_loop_stop()` before the program ends. The event loop thread will prevent the program from exiting.

## See Also

- [Async-Await](Async-Await.md) - Asynchronous function execution
- [Functions-and-Closures](Functions-and-Closures.md) - Function basics
- [Standard-Library](Standard-Library.md) - Promise utilities (`await_all`, `await_any`, etc.)
