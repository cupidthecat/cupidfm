#!/usr/bin/env python3
"""Queued movement must survive a slow plugin and preserve the final command."""
import fcntl
import os
from pathlib import Path
import pty
import select
import signal
import struct
import subprocess
import tempfile
import termios
import time

binary = Path(os.environ.get("CUPIDFM_TEST_BINARY", Path(__file__).resolve().parents[1] / "cupidfm"))
with tempfile.TemporaryDirectory(prefix="cupidfm-scroll-") as temporary:
    base = Path(temporary); home = base / "home"; directory = base / "files"
    home.mkdir(); directory.mkdir(); (home / ".config").mkdir()
    (home / ".cupidfmrc").write_text("# Default bindings\n")
    plugins = home / ".cupidfm/plugins"; plugins.mkdir(parents=True)
    ready = base / "ready"; receipt = base / "receipt"
    (plugins / "queue.cs").write_text("""let moves = 0;
fn on_load() { assert(write_file(getenv("CUPIDFM_SCROLL_READY"), "ready"), "ready receipt"); }
fn on_key(key) {
    if (key == "KEY_DOWN") {
        moves = moves + 1;
        if (moves == 1) { fm.exec("sleep", ["0.2"]); }
    }
    if (key == "!") {
        assert(write_file(getenv("CUPIDFM_SCROLL_RECEIPT"), to_str(moves) + "\\n" + to_str(fm.cursor())), "scroll receipt");
        return true;
    }
    return false;
}
""")
    for i in range(300): (directory / f"entry-{i:04d}.txt").write_text("scroll test\n")
    primary, secondary = pty.openpty()
    fcntl.ioctl(secondary, termios.TIOCSWINSZ, struct.pack("HHHH", 30, 100, 0, 0))
    env = dict(os.environ, TERM="xterm-256color", HOME=str(home), XDG_CONFIG_HOME=str(home / ".config"),
               CUPIDFM_SCROLL_READY=str(ready), CUPIDFM_SCROLL_RECEIPT=str(receipt))
    with tempfile.TemporaryFile() as errors:
        process = subprocess.Popen([str(binary), str(directory)], env=env, stdin=secondary,
                                   stdout=secondary, stderr=errors, start_new_session=True)
        os.close(secondary)
        sent = False; resize_sent = False; output = bytearray(); elapsed = None
        deadline = time.monotonic() + float(os.environ.get("CUPIDFM_TEST_TIMEOUT", "20"))
        try:
            while time.monotonic() < deadline and process.poll() is None:
                readable, _, _ = select.select([primary], [], [], 0.02)
                if readable:
                    try: output.extend(os.read(primary, 65536))
                    except OSError: break
                if ready.exists() and not sent:
                    started = time.monotonic(); os.write(primary, b"\x1bOB" * 100 + b"!"); sent = True
                if sent and not resize_sent and time.monotonic() - started > 0.05:
                    process.send_signal(signal.SIGWINCH); resize_sent = True
                if receipt.exists():
                    elapsed = time.monotonic() - started
                    break
            if not receipt.exists():
                errors.seek(0)
                raise AssertionError(("Queued scroll keys or final command lost", sent, process.poll(), errors.read().decode(errors="replace"), bytes(output[-5000:])))
            assert receipt.read_text() == "100\n100", receipt.read_text()
            os.write(primary, b"\x1bOP"); process.wait(timeout=10)
            errors.seek(0); error = errors.read().decode(errors="replace")
            assert process.returncode == 0, (process.returncode, error)
            assert "AddressSanitizer" not in error and "runtime error:" not in error, error
        finally:
            if process.poll() is None: process.kill(); process.wait()
            os.close(primary)
    print(f"100 queued scroll keys and the final command passed through a 200 ms plugin stall and resize ({elapsed:.3f}s)")
