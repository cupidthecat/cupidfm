// EXPECT_FAIL

let p = promise();
reject(p, "bad");

// awaiting a rejected promise should throw
await p;
