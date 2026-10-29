async fn pending(value) { return value; }
let job = pending("queued argument");
class Worker {
    async fn run(value) { return value; }
}
let worker = Worker();
let method_job = worker.run(["queued method argument"]);
// Neither task is awaited; VM teardown owns the queued functions and scopes.
