# Async / Await

## Table of Contents

- [Async Functions](#async-functions)
- [Await Operator](#await-operator)
- [Notes](#notes)
- [Promise Helpers](#promise-helpers)
- [Promise Combinators](#promise-combinators)
- [Sleep](#sleep)

CupidScript supports `async` functions, promises, and the `await` operator. Async calls return a promise and run on a cooperative, single-threaded scheduler. `await` blocks the current execution until the promise resolves (or throws if it rejects).

## Async Functions

```c
async fn add(a, b) {
  return a + b;
}

let v = await add(2, 3); // 5
```

Async function literals are also supported:

```c
let twice = async fn(x) { return x * 2; };
print(await twice(4)); // 8
```

## Await Operator

`await expr` evaluates `expr`. If the value is a promise, it runs the scheduler until the promise resolves and returns its value. If the promise rejects, `await` throws the rejection value.

```c
let v = await (1 + 2); // 3

// Awaiting a promise
let p = promise();
resolve(p, 42);
print(await p); // 42
```

## Notes

* Async functions return a promise and are scheduled for execution.
* `await` blocks until a promise resolves and throws on rejection.
* Without the event loop, the scheduler runs cooperatively; operations execute one at a time when awaited.
* With the event loop (Linux/Unix), multiple async operations can progress concurrently in the background.
* **Network I/O functions** (tcp_connect, socket_send, socket_recv, http_get, etc.) are inherently asynchronous and should be awaited.

## Background Event Loop

CupidScript supports an optional background event loop that runs the scheduler, timers, and pending network I/O in a dedicated thread. This enables **true async** behavior where multiple operations progress concurrently.

**Quick Start:**

```cs
event_loop_start();

// Multiple operations run concurrently
let p1 = sleep(100);
let p2 = sleep(100);
await p1;  // Both timers run in parallel
await p2;  // Total time: ~100ms, not 200ms

event_loop_stop();
```

**API Functions:**

* `event_loop_start() -> bool` - Start the background event loop (returns `true` on success, `false` on Windows or if already running)
* `event_loop_stop() -> bool` - Stop the background event loop (returns `true`)
* `event_loop_running() -> bool` - Check if the event loop is running

**Platform Support:**

* **Linux/macOS/Unix**: Full support with pthread-based background thread
* **Windows**: Returns `false`; falls back to cooperative scheduling

**When to Use:**

Use the event loop when you want:
- Multiple async operations to run concurrently
- Non-blocking timers and I/O
- True asynchronous behavior

Without the event loop, async/await still works but operations execute sequentially when awaited.

For detailed documentation, see [Event-Loop](Event-Loop.md).

## Promise Helpers

### `promise() -> promise`

Creates a new pending promise.

### `resolve(promise, value = nil) -> bool`

Resolves a promise. Returns `true` if it was pending.

### `reject(promise, value = nil) -> bool`

Rejects a promise. Returns `true` if it was pending.

### `is_promise(value) -> bool`

Checks if a value is a promise.

## Promise Combinators

### `await_all(list[promise]) -> list`

Waits for all promises to resolve. If any reject, the first rejection is thrown.

### `await_any(list[promise]) -> value`

Waits until any promise resolves and returns its value. If all reject, throws the last rejection.

### `await_all_settled(list[promise]) -> list`

Waits for all promises to settle and returns a list of result maps:

```c
// Each result: {status: "fulfilled"|"rejected", value: any}
```

### `timeout(promise, ms) -> promise`

Returns a new promise that resolves/rejects like the input, but rejects if `ms` elapses first.

## Sleep

`sleep(ms)` returns a promise that resolves after `ms` milliseconds.

```c
async fn delayed(msg) {
  await sleep(10);
  return "done: " + msg;
}

print(await delayed("work"));
```
