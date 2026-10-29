// Demonstrates now_ms() + sleep(ms)

let t0 = now_ms();
await sleep(50);
let t1 = now_ms();
print("dt_ms =", t1 - t0);

