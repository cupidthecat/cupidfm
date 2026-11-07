#!/usr/bin/env python3
"""Inspect and copy literal fields across files, selections, special files, and resize."""
import base64
import errno
import fcntl
import os
from pathlib import Path
import pty
import re
import signal
import struct
import subprocess
import tempfile
import termios
import time

from terminal_wait import wait_for_terminal

binary = Path(os.environ.get("CUPIDFM_TEST_BINARY", Path(__file__).resolve().parents[1] / "cupidfm"))
with tempfile.TemporaryDirectory(prefix="cupidfm-inspector-") as temporary:
    root = Path(temporary)
    home = root / "home"; home.mkdir()
    source = root / "files"; source.mkdir()
    literal = "a\nnotes.txt"
    (source / literal).write_text("notes")
    (source / "b.json").write_text('{"value": 1}')
    (source / "child").mkdir()
    (source / "broken").symlink_to("missing")
    os.mkfifo(source / "fifo.pipe")
    (home / ".cupidfmrc").write_text("keymap=vim\n")
    plugins = home / ".cupidfm/plugins"; plugins.mkdir(parents=True)
    (plugins / "ready.cs").write_text(r'''
let checks = 0;
fn on_load() { write_file(getenv("INFO_ROOT") + "/ready", "ready"); }
fn on_key(key) {
    if (key == "!") {
        checks = checks + 1;
        let base = getenv("INFO_ROOT") + "/check-" + to_str(checks);
        write_file(base + "-name", fm.selected_name());
        write_file(base + "-count", to_str(len(fm.selected_paths())));
        write_file(base, "done");
        return true;
    }
    return false;
}
''')
    for profile in ("vim", "cupidfm"):
        (root / "ready").unlink(missing_ok=True)
        for receipt in root.glob("check-*"): receipt.unlink()
        primary, secondary = pty.openpty()
        fcntl.ioctl(secondary, termios.TIOCSWINSZ, struct.pack("HHHH", 32, 120, 0, 0))
        env = dict(os.environ, HOME=str(home), TERM="xterm-256color", INFO_ROOT=str(root))
        for name in ("DISPLAY", "WAYLAND_DISPLAY", "TMUX"):
            env.pop(name, None)
        with tempfile.TemporaryFile() as errors:
            process = subprocess.Popen([str(binary), "--keymap=" + profile, str(source / literal)],
                                       env=env, stdin=secondary, stdout=secondary, stderr=errors,
                                       start_new_session=True)
            os.close(secondary); output = bytearray(); checks = 0
            def wait(predicate, timeout=10):
                wait_for_terminal(process, primary, predicate, output, errors, timeout)
            def send(keys): os.write(primary, keys)
            def clipboard():
                return re.findall(rb"\x1b]52;c;([A-Za-z0-9+/=]*)\x1b\\", bytes(output))
            def copy(keys=b""):
                count = len(clipboard())
                send(keys + b"cc")
                wait(lambda: len(clipboard()) > count)
                return base64.b64decode(clipboard()[-1]).decode()
            def snapshot(keys=b""):
                global checks
                checks += 1; receipt = root / f"check-{checks}"
                send(keys + b"!")
                wait(receipt.exists)
                return (Path(str(receipt) + "-name").read_text(),
                        Path(str(receipt) + "-count").read_text())
            try:
                wait((root / "ready").exists)
                if profile == "cupidfm":
                    snapshot(b",a")
                opener = b"\t" if profile == "vim" else b"\x14"
                assert copy(opener) == literal
                assert copy(b"j") == str(source / literal)
                actual = copy(b"l")
                assert actual == str(source / "b.json"), (profile, actual, str(source / "b.json"))
                assert copy(b"k") == "b.json"
                assert snapshot(b"\t")[0] == "b.json"
                if profile == "vim":
                    assert snapshot(b"gg")[0] == "child"
                    first_size = copy(b"\tG")
                    assert first_size == "Calculating" or first_size.startswith("0 bytes")
                    deadline = time.monotonic() + 2
                    wait(lambda: time.monotonic() >= deadline)
                    assert copy().startswith("0 bytes")
                    assert snapshot(b"\t")[0] == "child"
                    snapshot(b"j  ")
                    assert copy(b"\t") == literal
                    assert copy(b"l") == "b.json"
                    # Escape closes even with a copy chord pending.
                    assert snapshot(b"c\x1b") == ("b.json", "2")
                    snapshot(b"\x1bf^fifo[.]pipe$\n")
                    assert copy(b"\tjj") == "FIFO"
                    snapshot(b"\t\x1b")
                    snapshot(b"f^broken$\n")
                    assert copy(b"\tG") == "missing"
                    (source / "broken").unlink()
                    assert copy(b"r") == os.strerror(errno.ENOENT)
                    snapshot(b"\x1b")
                    snapshot(b"f\x15^b[.]json$\n")
                    assert copy(b"\t") == "b.json"
                    fcntl.ioctl(primary, termios.TIOCSWINSZ, struct.pack("HHHH", 4, 8, 0, 0))
                    process.send_signal(signal.SIGWINCH)
                    assert copy() == "b.json"
                    fcntl.ioctl(primary, termios.TIOCSWINSZ, struct.pack("HHHH", 32, 120, 0, 0))
                    process.send_signal(signal.SIGWINCH)
                    snapshot(b"\x1b")
                send(b"q" if profile == "vim" else b"\x1bOP")
                wait(lambda: process.poll() is not None)
                errors.seek(0); error = errors.read()
                assert process.returncode == 0 and b"Sanitizer" not in error and b"runtime error:" not in error, error
            finally:
                if process.poll() is None: process.kill(); process.wait()
                os.close(primary)
print("Inspector: literal field copy, file swipes, selections, FIFO, broken/deleted links, resize, and both keymaps passed")
