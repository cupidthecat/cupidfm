#!/usr/bin/env python3
"""Check streamed recursive views, physical file operations, tab ownership, and cancellation."""
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
with tempfile.TemporaryDirectory(prefix="cupidfm-recursive-") as temporary:
    root = Path(temporary); home = root / "home"; home.mkdir()
    source = root / "files"; source.mkdir()
    nested = source / "nested"; nested.mkdir()
    target = root / "target"; target.mkdir()
    (home / ".cupidfmrc").write_text("keymap=vim\n")
    for name, content in {"top.txt": "Needle", "nested/a.txt": "needle", "nested/b.md": "NEEDLE",
                          "nested/line\nbreak.txt": "Needle", ".hidden.txt": "Needle",
                          "ignored.txt": "Needle", ".ignore": "ignored.txt\n"}.items():
        (source / name).write_text(content)
    plugins = home / ".cupidfm/plugins"; plugins.mkdir(parents=True)
    (plugins / "inspect.cs").write_text(r'''
let checks = 0;
fn on_load() { write_file(getenv("CUPIDFM_SEARCH_ROOT") + "/ready", "ready"); }
fn on_key(key) {
    if (key == "!") {
        checks = checks + 1;
        write_file(getenv("CUPIDFM_SEARCH_ROOT") + "/check-" + to_str(checks),
            to_str(len(fm.entries())) + "\n" + fm.cwd() + "\n" + fm.selected_name());
        return true;
    }
    return false;
}
''')
    primary, secondary = pty.openpty()
    fcntl.ioctl(secondary, termios.TIOCSWINSZ, struct.pack("HHHH", 32, 120, 0, 0))
    env = dict(os.environ, TERM="xterm-256color", HOME=str(home), CUPIDFM_SEARCH_ROOT=str(root))
    env.pop("DISPLAY", None); env.pop("WAYLAND_DISPLAY", None)
    with tempfile.TemporaryFile() as errors:
        process = subprocess.Popen([str(binary), str(source)], env=env, stdin=secondary,
                                   stdout=secondary, stderr=errors, start_new_session=True)
        os.close(secondary); output = bytearray(); checks = 0
        def wait_for(predicate, timeout=15):
            deadline = time.monotonic() + timeout
            while time.monotonic() < deadline:
                if predicate():
                    return
                if process.poll() is not None:
                    break
                ready, _, _ = select.select([primary], [], [], 0.02)
                if ready:
                    try: output.extend(os.read(primary, 65536))
                    except OSError: break
            errors.seek(0)
            raise AssertionError((process.poll(), errors.read().decode(errors="replace"),
                                  bytes(output[-2500:])))
        def snapshot(keys=b""):
            global checks
            checks += 1
            receipt = root / f"check-{checks}"
            os.write(primary, keys + b"!")
            wait_for(receipt.exists)
            return receipt.read_text().split("\n", 2)
        try:
            wait_for((root / "ready").exists)
            assert snapshot()[0] == "3"
            snapshot(b"sa\\.txt$\n")
            wait_for(lambda: snapshot()[0] == "1")
            assert snapshot()[2] == "nested/a.txt"
            snapshot(b"y")
            snapshot(b"g " + str(target).encode() + b"\n")
            os.write(primary, b"p")
            wait_for((target / "a.txt").exists)
            assert (target / "a.txt").read_text() == "needle"
            snapshot(b"g " + str(source).encode() + b"\n")

            snapshot(b"stop\\.txt$\n")
            wait_for(lambda: snapshot()[0] == "1")
            assert snapshot(b"tt")[0] == "3"
            snapshot(b"1")
            wait_for(lambda: snapshot()[0] == "1")
            assert snapshot()[2] == "top.txt"
            assert snapshot(b"\x03")[0] == "3", "closing a search tab preserves the other tab"

            snapshot(b"SNeedle\n")
            wait_for(lambda: snapshot()[0] == "2")
            assert snapshot(b"gg")[2] == "nested/line\nbreak.txt"
            snapshot(b"y")
            snapshot(b"g " + str(target).encode() + b"\n")
            os.write(primary, b"p")
            wait_for((target / "line\nbreak.txt").exists)
            assert (target / "line\nbreak.txt").read_text() == "Needle"
            snapshot(b"g " + str(source).encode() + b"\n")

            snapshot(b"snested$\n")
            wait_for(lambda: snapshot()[0] == "1")
            assert snapshot(b"l")[1] == str(nested)
            assert snapshot(b"h")[2] == "nested"
            snapshot(b"s\\.txt$\n")
            wait_for(lambda: snapshot()[0] == "3")
            assert snapshot(b"fa\\.txt$\n")[0] == "1"
            assert snapshot(b"\x1b")[0] == "3", "Esc clears the filter before leaving search"
            assert snapshot(b"\x13")[2] == "nested", "Ctrl+S restores the original hover"
            os.write(primary, b"q")
            process.wait(timeout=10)
            errors.seek(0); error = errors.read().decode(errors="replace")
            assert process.returncode == 0, error
            assert "AddressSanitizer" not in error and "runtime error:" not in error, error
        finally:
            if process.poll() is None:
                process.kill(); process.wait()
            os.close(primary)

    # A slow backend must be stopped and reaped when leaving its view.
    (root / "ready").unlink()
    for receipt in root.glob("check-*"): receipt.unlink()
    programs = root / "bin"; programs.mkdir()
    slow = programs / "fd"
    slow.write_text('''#!/usr/bin/python3
import os,sys,time
from pathlib import Path
Path(os.environ["CUPIDFM_SEARCH_ROOT"]+"/search-pid").write_text(str(os.getpid()))
sys.stdout.buffer.write(b"nested/a.txt"+bytes([0]));sys.stdout.flush()
time.sleep(60)
''')
    slow.chmod(0o755)
    primary, secondary = pty.openpty()
    fcntl.ioctl(secondary, termios.TIOCSWINSZ, struct.pack("HHHH", 32, 120, 0, 0))
    env["PATH"] = str(programs) + os.pathsep + env["PATH"]
    with tempfile.TemporaryFile() as errors:
        process = subprocess.Popen([str(binary), str(source)], env=env, stdin=secondary,
                                   stdout=secondary, stderr=errors, start_new_session=True)
        os.close(secondary); output = bytearray(); checks = 0
        try:
            wait_for((root / "ready").exists)
            os.write(primary, b"sanything\n")
            wait_for((root / "search-pid").exists)
            child = int((root / "search-pid").read_text())
            wait_for(lambda: snapshot()[0] == "1")
            started = time.monotonic()
            assert snapshot(b"\x13")[0] == "3"
            assert time.monotonic() - started < 2
            try:
                os.kill(child, 0)
            except ProcessLookupError:
                pass
            else:
                raise AssertionError("cancelled search child was not reaped")
            os.write(primary, b"q")
            process.wait(timeout=10)
            errors.seek(0); error = errors.read().decode(errors="replace")
            assert process.returncode == 0, error
            assert "AddressSanitizer" not in error and "runtime error:" not in error, error
        finally:
            if process.poll() is None:
                process.kill(); process.wait()
            os.close(primary)
print("Recursive views, nested/newline paths, copy, tab ownership, filtering, and child cancellation passed")
