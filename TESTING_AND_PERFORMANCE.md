# Tests and performance

Build CupidFM before running terminal tests:

```sh
make -j
make test
make test-libs
python3 tests/test_terminal_smoke.py
python3 tests/test_scroll_smoke.py
python3 tests/test_library_dependencies.py
```

Application tests cover containers and paths, file commands and history, editor loading and atomic saves, configuration keys, plugin execution, MIME invalidation, text/archive previews, and allocation failures. Search tests include 257 matching entries. The scrolling test queues 100 movements through a 200 ms plugin stall and resize, then checks the final cursor and command.

The bundled library suites run from their individual source folders. [lib/sources.json](lib/sources.json) records the upstream revisions being tested.

## Sanitizers

```sh
make -j CFLAGS='-Wall -Wextra -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer'
python3 tests/test_terminal_smoke.py
python3 tests/test_scroll_smoke.py
```

Hosted CI also rebuilds and runs focused allocation, MIME, input-queue, editor, configuration, plugin, and preview tests with sanitizers. Build outputs use compiler/flag-specific directories. Return to the default optimized application with `make -j`.

## Measurements

[PERFORMANCE.md](PERFORMANCE.md) records the generated inputs, baseline commit, timing scope, and measured results. The terminal benchmark measures first-preview latency and process CPU time; it does not report frame rate.

```sh
python3 tests/benchmark_terminal.py --runs 3 > /tmp/cupidfm-benchmark.json
```

Use `--baseline /path/to/another/cupidfm` to compare builds with the same workload. Text preview regressions separately enforce bounded reads and verify checkpoint reuse near line 320,000, including syntax state, upward scrolling, reloads, and edits.

Image regressions cover thumbnail/output reuse, file edits with restored modification times, resize/movement, backend changes, missing files, premultiplied alpha, bounded memory, allocation failures, and retry behavior. `tests/benchmark_image_preview.py` measures protocol output/CPU through a PTY; the performance guide records the workload and scope.
