# Browser responsiveness

The browser coalesces repeated movement frames while applying every key to the cursor,
selection, and plugin hooks. Both arrow keys and Vim j/k bursts use this path. Commands
after a movement burst remain queued in order.

Input, directory-watch events, task completion, and streamed search results repaint immediately.
Idle frames refresh at most every 250 ms, subject to the input polling interval, so task
progress and directory-size estimates remain visible without repainting every pane on each poll.
The status line counts pending tasks without copying their error/output buffers. Unchanged
history entries reuse their strings, and parent panes reuse the current child position.

Syntax initialization reuses the parsed configuration and skips compilation of definitions
already supplied by a higher-priority directory. CupidConf compares literal lookup keys directly;
wildcard and escaped keys keep their existing matching behavior and duplicate-key order.
The vendored library matches its upstream source pin.

## Repeat the measurement

    python3 tests/benchmark_browser.py /path/to/baseline ./cupidfm --runs 3

The harness creates 5,000 files and an isolated home directory, loads the same bundled syntax
directory for both binaries, waits idle for two seconds, then queues 100 j moves and a plugin
receipt. It checks the final cursor and reports child CPU time and command latency.
CPU time includes startup and shutdown. PTY timings exclude a real terminal's rendering cost.

On the development WSL host, three runs had median CPU times of 0.362 seconds before and
0.064 seconds after these changes, about 82% lower. Median queued-movement latency fell
from 59 ms to 2.7 ms. These figures describe this fixture and host, not every directory or workload.

A separate CupidConf measurement used 20,000 lookups of the final key in a 1,000-key
configuration: 0.258 CPU seconds before and 0.066 after. Lookup remains linear in entry count;
the improvement removes wildcard matching from literal lookups.

## Dialog and indicator behavior

Uppercase Q or Escape closes help in one press, including during fuzzy-query editing or
with filtered results displayed. Lowercase q remains query text while editing.
Browser Escape leaves find, filtering, and recursive search together; outside search it
finishes visual selection or clears selection. Path-prompt cancellation retains its existing
rollback behavior.

File icons, Git indicators, and filenames use fixed terminal-cell columns. A one-cell icon
cannot shift a Git indicator into a neighboring column. One-cell Unicode file symbols
request text presentation explicitly, preventing terminal fonts from expanding them to emoji
width after ncurses has computed the row positions. Two-cell file emoji and Git indicators
retain their normal presentation.

These changes do not make every preview asynchronous or remove directory scan costs.
Archive decoding, image decoding, remote storage latency, and large-directory scans need
separate workload measurements before further changes.
