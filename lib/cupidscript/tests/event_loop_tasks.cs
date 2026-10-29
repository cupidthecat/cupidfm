// Test event loop with async function calls
event_loop_start();

async fn add(a, b) {
    await sleep(10);
    return a + b;
}

async fn multiply(a, b) {
    await sleep(20);
    return a * b;
}

let p1 = add(5, 3);
let p2 = multiply(4, 7);

assert(await p1 == 8, "add(5, 3) should be 8");
assert(await p2 == 28, "multiply(4, 7) should be 28");

event_loop_stop();

print("event loop tasks ok");
