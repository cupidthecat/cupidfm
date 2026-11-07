#!/usr/bin/env python3
"""CLI output paths resolve against the launch directory and follow chooser/quit semantics."""
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
with tempfile.TemporaryDirectory(prefix="cupidfm-cli-output-") as temporary:
    root = Path(temporary)
    home = root / "home"; home.mkdir()
    (home / ".cupidfmrc").write_text("# default keymap\n")
    source = root / "source"; source.mkdir()
    (source / "selected.txt").write_text("data")
    plugins = home / ".cupidfm/plugins"; plugins.mkdir(parents=True)
    (plugins / "ready.cs").write_text('fn on_load() { write_file(getenv("CUPIDFM_READY"), "ready"); }')
    def launch(options, keys):
        ready_path = root / "ready"
        ready_path.unlink(missing_ok=True)
        primary, secondary = pty.openpty()
        fcntl.ioctl(secondary, termios.TIOCSWINSZ, struct.pack("HHHH", 30, 100, 0, 0))
        env = dict(os.environ, TERM="xterm-256color", HOME=str(home), CUPIDFM_READY=str(ready_path))
        with tempfile.TemporaryFile() as errors:
            process = subprocess.Popen([str(binary), "--keymap=yazi", *options, "source"], cwd=root,
                                       env=env, stdin=secondary, stdout=secondary, stderr=errors,
                                       start_new_session=True)
            os.close(secondary)
            try:
                sent = False
                deadline = time.monotonic() + 10
                while time.monotonic() < deadline and process.poll() is None:
                    readable, _, _ = select.select([primary], [], [], 0.02)
                    if readable:
                        try: os.read(primary, 65536)
                        except OSError: break
                    if ready_path.exists() and not sent:
                        os.write(primary, keys); sent = True
                process.wait(timeout=2)
                errors.seek(0); error = errors.read().decode(errors="replace")
                assert process.returncode == 0, error
                assert "AddressSanitizer" not in error and "runtime error:" not in error, error
            finally:
                if process.poll() is None:
                    process.kill(); process.wait()
                os.close(primary)
    launch(["--cwd-file=cwd.txt"], b"hq")
    assert (root / "cwd.txt").read_text() == str(root)
    launch(["--cwd-file=skipped.txt"], b"Q")
    assert not (root / "skipped.txt").exists()
    launch(["--chooser-file", "chosen.txt"], b"\n")
    assert (root / "chosen.txt").read_text() == str(source / "selected.txt") + "\n"
    for arguments in (["--keymap=unknown"], ["--cwd-file"], ["--chooser-file="]):
        result = subprocess.run([str(binary), *arguments], cwd=root, capture_output=True, timeout=5)
        assert result.returncode == 2, (arguments, result.returncode)
print("CLI keymap override, cwd output, Q suppression, chooser output, and option validation passed")
