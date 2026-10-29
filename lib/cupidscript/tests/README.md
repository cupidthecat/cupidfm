# Tests

CupidScript tests are plain `.cs` scripts that use the stdlib `assert(...)` function.

## Running

From the repo root:

- `make test` (builds `bin/cupidscript` and runs all tests)

The test runner is [tests/run_tests.sh](tests/run_tests.sh).

## Conventions

- Any `tests/*.cs` file is treated as a test.
- Files starting with `_` are ignored (helpers / fixtures).
- A test passes if the script exits with status 0.
- A test fails if the VM returns a non-zero exit code (parse error, runtime error, failed assert, etc.).

### Negative tests

If a test is expected to fail (parse error or runtime error), add this line anywhere in the file:

- `// EXPECT_FAIL`

The runner will treat a non-zero exit as PASS for that test.
