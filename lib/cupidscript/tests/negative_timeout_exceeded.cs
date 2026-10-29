// EXPECT_FAIL
// Trigger the VM wall-clock timeout guard.

set_timeout(1);   // 1ms
await sleep(25);        // exceed timeout while awaiting sleep

// Do at least one VM step after sleeping so the timeout check triggers.
let x = 0;
x = x + 1;

// And keep running in case the check happens in the loop.
while (true) {
  x = x + 1;
}
