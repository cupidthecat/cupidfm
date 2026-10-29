# Tests

## Table of Contents

- [Running Tests](#running-tests)
- [Writing Script Tests](#writing-script-tests)
- [Negative Tests (Expected Failures)](#negative-tests-expected-failures)
- [Helper Files](#helper-files)
- [C API Tests](#c-api-tests)

CupidScript tests are plain `.cs` scripts under `tests/` plus a native C test binary.

## Running Tests

```sh
make test
```

The runner executes each `tests/*.cs` file in lexical order and prints PASS/FAIL. It also runs the C API test binary `bin/c_api_tests`.

## Writing Script Tests

1. Create a new file in `tests/` (e.g. `tests/my_feature.cs`).
2. Use `assert(condition, "message")` for checks.
3. Optionally print a success marker at the end.

Example:

```c
// tests/my_feature.cs
let x = 1 + 2;
assert(x == 3, "basic math");
print("my_feature ok");
```

## Negative Tests (Expected Failures)

If a test should fail, add this header at the top:

```c
// EXPECT_FAIL
```

Example:

```c
// tests/negative_example.cs
// EXPECT_FAIL
assert(false, "should fail");
```

## Helper Files

Files starting with `_` are ignored by the test runner. Use these for shared fixtures or helper modules.

## C API Tests

`tests/c_api_tests.c` is compiled into `bin/c_api_tests` and run by `make test`. It exercises the embedding API directly (lists, maps, error handling, etc.).

## Sanitizer builds

Use separate object and binary directories:

```sh
make -j2 all OBJDIR=obj-asan BINDIR=bin-asan CFLAGS='-std=c99 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer' LDFLAGS='-fsanitize=address,undefined -lssl -lcrypto'
BIN="$PWD/bin-asan/cupidscript" API_TEST_BIN="$PWD/bin-asan/c_api_tests" bash tests/run_tests.sh
```

The runner checks sanitizer diagnostics and signals even for `EXPECT_FAIL` scripts. Coverage includes tuple host ownership, tuple comprehension bindings, container cycles, pending async tasks, and cleanup error/control-flow precedence.
