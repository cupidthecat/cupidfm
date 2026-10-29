// Example: start background event loop and run async tasks
print("Starting background event loop...");
if (!event_loop_start()) print("Event loop already running");

async fn worker(id) {
  for (let i = 0; i < 3; i = i + 1) {
    await sleep(100);
    print("worker", id, "tick", i);
  }
  return id;
}

let t1 = worker(1);
let t2 = worker(2);
let r = await await_all([t1, t2]);
print("Workers finished:", r);

print("Stopping event loop");
event_loop_stop();
