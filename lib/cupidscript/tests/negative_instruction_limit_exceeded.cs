// EXPECT_FAIL
// Trigger the VM instruction limit guard.

set_instruction_limit(2000);

let x = 0;
while (true) {
  x = x + 1;
}
