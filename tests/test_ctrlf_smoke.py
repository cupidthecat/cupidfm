#!/usr/bin/env python3
"""One Escape cancels original-profile search and preserves the next command."""
import fcntl
import os
from pathlib import Path
import pty
import select
import struct
import subprocess
import tempfile
import termios
import time

binary = Path(os.environ.get("CUPIDFM_TEST_BINARY", Path(__file__).resolve().parents[1] / "cupidfm"))
with tempfile.TemporaryDirectory(prefix="cupidfm-ctrlf-") as temporary:
    root = Path(temporary)
    home = root / "home"; home.mkdir()
    source = root / "files"; source.mkdir()
    for index in range(100):
        (source / f"entry-{index:03}.bin").touch()
    (home / ".cupidfmrc").write_text("keymap=cupidfm\n")
    plugins = home / ".cupidfm/plugins"; plugins.mkdir(parents=True)
    (plugins / "ready.cs").write_text(r'''
let checks = 0;
fn on_load() { write_file(getenv("CHECK_ROOT") + "/ready", "ready"); }
fn on_key(key) {
    if (key == "!") {
        checks = checks + 1;
        assert(!fm.search_active(), "search remained active after Escape");
        assert(fm.search_query() == "", "query remained after Escape");
        assert(len(fm.entries()) == 100, "listing remained filtered");
        write_file(getenv("CHECK_ROOT") + "/" + to_str(checks), "done");
        return true;
    }
    return false;
}
''')
    primary, secondary = pty.openpty()
    fcntl.ioctl(secondary, termios.TIOCSWINSZ, struct.pack("HHHH", 30, 100, 0, 0))
    environment = dict(os.environ, HOME=str(home), TERM="xterm-256color", CHECK_ROOT=str(root))
    with tempfile.TemporaryFile() as errors:
        process = subprocess.Popen([str(binary), str(source)], env=environment, stdin=secondary,
                                   stdout=secondary, stderr=errors, start_new_session=True)
        os.close(secondary)
        output = bytearray()
        def wait(predicate, timeout=10):
            deadline = time.monotonic() + timeout
            while time.monotonic() < deadline and process.poll() is None:
                if predicate():
                    return
                if select.select([primary], [], [], 0.01)[0]:
                    try: output.extend(os.read(primary, 65536))
                    except OSError: break
            errors.seek(0)
            raise AssertionError((process.poll(), errors.read(), bytes(output[-2000:])))
        try:
            wait((root / "ready").exists)
            cases = (b"\x06entry-01\x1b!", b"\x06\x1b!",
                     b"\x06entry-01\n\x1b!", b"\x06entry-01\x1b!")
            for index, keys in enumerate(cases, start=1):
                os.write(primary, keys)
                wait((root / str(index)).exists)
            os.write(primary, b"\x06entry-01")
            deadline = time.monotonic() + 0.4
            wait(lambda: time.monotonic() >= deadline)
            os.write(primary, b"\x1b!")
            wait((root / "5").exists)
            os.write(primary, b"\x1bOP")
            process.wait(timeout=5)
            errors.seek(0); error = errors.read()
            assert process.returncode == 0 and b"Sanitizer" not in error and b"runtime error:" not in error, error
        finally:
            if process.poll() is None:
                process.kill(); process.wait()
            os.close(primary)
print("Ctrl+F: one Escape closes empty, typed, submitted, repeated, and idle searches; following keys survive")
