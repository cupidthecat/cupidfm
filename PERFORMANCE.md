# Preview performance

Text previews read physical lines until the visible range is filled. Earlier lines are scanned for syntax state and discarded. The reader retains one line buffer instead of allocating a copy of every line in the file. Delimiters outside the visible columns still affect the following line.

## Focused measurement

A generated 32 MiB input with short text lines was used to compare the first-screen reader with the full-file allocation loop from commit `b18c2fd13439893cc432e0158ab6142e572b8e5b`. Both harnesses were compiled with `-O2`; each ran five times on the same WSL host.

| Reader | Bytes consumed | Median elapsed time |
| --- | ---: | ---: |
| Full-file allocation loop | 33,554,432 | 136.424 ms |
| Visible-range reader | 8,192 | 0.032 ms |

These figures measure the text reader, not total application startup, MIME detection, disk throughput, or frame rate. The generated input was supplied through a buffered stream. The new path drew the viewport; the comparison loop read, allocated, and freed the full input without drawing it. The read budget is the regression criterion; elapsed time varies by host.

## Reproduce the current reader

```sh
make -B -C tests test_text_preview CFLAGS='-O2 -g -std=c2x -Wall -Wextra'
./tests/test_text_preview
./tests/test_text_preview --benchmark
```

The benchmark emits JSON with input size, bytes consumed, and elapsed seconds. The regression test checks that a first-screen preview consumes less than 64 KiB of a 32 MiB stream. It also checks physical lines longer than 255 bytes, scrolled comments, quoted delimiters, comments beyond the visible columns, CRLF, EOF, and read failures.

Downward text scrolling checks only whether another line exists beyond the viewport; it does not count the rest of the file. Archive scrolling counts entries rather than newline bytes in compressed data.

Text previews keep at most 2,048 checkpoints with byte offsets and syntax state. Checkpoints start 128 lines apart and are thinned as the index grows. After the initial scan, nearby scrolling starts from a checkpoint instead of rereading the entire prefix. The cache is rebuilt when file identity, metadata, or syntax delimiters change; recent changes bypass reuse to avoid coarse timestamp races. A single very long physical line can grow the line buffer to that line's size. Image decoding, recursive directory sizes, Git content hashing, and archive previews have separate costs; this measurement does not qualify their performance.


## Queued scrolling

Movement keys are processed in order, including plugin hooks, while queued movement can share a repaint within a 16 ms frame budget. Slow frames do not discard pending input. Non-movement commands remain queued and are processed in order. The terminal regression sends 100 down-arrow keys and a final command through a deliberate 200 ms plugin stall and a resize; all movements must reach cursor index 100.

```sh
python3 tests/test_scroll_smoke.py
```

The prior build lost the queued command in this test. The fixed build completed the workload in about 250 ms in a local optimized run, including the deliberate 200 ms stall. This is a regression workload, not an input-latency guarantee.

## MIME classification

MIME results use a synchronized cache capped at 128 entries. A cache key includes the path, libmagic flags, device/inode, size, mode, and nanosecond modification/change times. Regular files changed in the current or previous second bypass cache hits, and directory reloads clear the cache. Results are copied into caller-owned buffers. Failed classifications are retried. Directory reloads also discard the text line index.

A short terminal profile attributed 360 ms of a large-file workload to 53 MIME classifications and 302 ms of a directory workload to 437 classifications. Reusing unchanged results removes repeated work from idle redraws and scrolling.

## Application workload

The comparison used a 30-by-100 terminal, an isolated home directory with default bindings, three runs per workload, and optimized builds on the same WSL host. The text file was 34,062,336 bytes. The directory case contained 1,000 small text files. After the first preview, each run waited 0.8 seconds, queued five preview-down keys, waited another 0.6 seconds, then exited. Generated files were allowed to leave the recent-change window before measurement. No Git repository or plugins were present.

| Workload | Baseline first preview | Updated first preview | Baseline CPU time | Updated CPU time |
| --- | ---: | ---: | ---: | ---: |
| Large text file | 92.3 ms | 23.9 ms | 1,090.1 ms | 33.0 ms |
| 1,000 entries | 23.1 ms | 20.8 ms | 348.9 ms | 29.6 ms |

Values are medians. The baseline is commit `9094668576c55354c94c46e15cdb1cc2471bcf19`; the updated measurements include bounded previews, bounded scroll checks, and MIME caching. CPU time covers the whole workload; it is not frame time. The numbers do not qualify remote filesystems, Git hashing, image decoders, archives, or arbitrary plugin work.

To measure the current optimized build:

```sh
make -j
python3 tests/benchmark_terminal.py --runs 3 > /tmp/cupidfm-benchmark.json
```

Pass `--baseline /path/to/another/cupidfm` to compare two builds with the same generated workload.

## Image previews

Image previews cache viewport-sized thumbnails and encoded output. Kitty uploads fit the pane instead of sending full-resolution pixels. Metadata, viewport, placement, and explicit redraws invalidate reuse. Recent file changes bypass metadata-only cache hits. The cache retains at most 16 MiB of pixels and 16 MiB of encoded output across four entries; decoder working memory is separate.

The image benchmark uses a 30-by-100 PTY, an isolated default configuration, two generated 2,048-by-1,536 BMPs, and three runs per protocol. It waits 0.35 seconds after the first protocol output, selects the next file, returns to the first at 0.70 seconds, then exits at 1.10 seconds. Files leave the recent-change window before measurement. The PTY is drained and protocol features are simulated through environment variables; the benchmark does not emulate a terminal renderer.

```sh
make -j
python3 tests/benchmark_image_preview.py --runs 3 > /tmp/cupidfm-image-benchmark.json
```

Pass `--baseline /path/to/cupidfm` for a paired comparison. The recorded baseline is `14396a5d9745dc10fc790746f36db3a4092d8372`. Measurements report process CPU, output bytes, and time to the first protocol bytes. They do not measure displayed-image completion, GPU latency, or frame rate.

| Protocol | Baseline first output | Updated first output | Baseline CPU | Updated CPU | Baseline output | Updated output |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| KITTY | 162.1 ms | 30.5 ms | 961.8 ms | 60.8 ms | 50.47 MB | 1.42 MB |
| ANSI | 20.9 ms | 23.2 ms | 72.1 ms | 39.5 ms | 0.05 MB | 0.21 MB |
| SIXEL | 88.9 ms | 80.3 ms | 344.4 ms | 163.4 ms | 1.48 MB | 1.48 MB |

Values are medians from three optimized runs on the same WSL host. The updated build includes CupidImage's merged decoder/conformance update and the output-equivalent Sixel optimization. ANSI now fills the correct character-cell aspect ratio, so its output is larger. Its first output is about 2 ms later in this workload while total CPU work is lower. Kitty sends fewer pixels, and returning to an unchanged Sixel image reuses its encoded output.

A separate Sixel encoder comparison is described in the [upstream performance notes](lib/cupidimage/docs/terminal-performance.md). Its output-equivalence tests preserve colors and dithering, including allocation-failure fallback.
