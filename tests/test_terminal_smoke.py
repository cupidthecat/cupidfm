#!/usr/bin/env python3
"""Start CupidFM in a temporary terminal and exit through its default F1 binding."""
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

binary = Path(__file__).resolve().parents[1] / "cupidfm"
with tempfile.TemporaryDirectory(prefix="cupidfm-terminal-") as directory:
    home = Path(directory)
    (home / ".config").mkdir()
    (home / ".cupidfmrc").write_text("# Use default bindings in this isolated test\n")
    (home / "file.txt").write_text("preview fixture\n")
    primary, secondary = pty.openpty()
    fcntl.ioctl(secondary, termios.TIOCSWINSZ, struct.pack("HHHH", 30, 100, 0, 0))
    env = dict(os.environ, TERM="xterm-256color", HOME=directory,
               XDG_CONFIG_HOME=str(home / ".config"))
    with (home / "stderr").open("wb") as errors:
        process = subprocess.Popen([str(binary)], cwd=home, env=env,
                                   stdin=secondary, stdout=secondary, stderr=errors,
                                   start_new_session=True)
        os.close(secondary)
        output = bytearray()
        deadline = time.monotonic() + 2
        try:
            while time.monotonic() < deadline and process.poll() is None:
                ready, _, _ = select.select([primary], [], [], 0.1)
                if ready:
                    try: output.extend(os.read(primary, 65536))
                    except OSError: break
            os.write(primary, b"\x1bOP")
            process.wait(timeout=10)
        finally:
            if process.poll() is None:
                process.kill()
                process.wait()
            os.close(primary)
    error_text = (home / "stderr").read_text(errors="replace")
    assert process.returncode == 0, (process.returncode, error_text)
    assert len(output) > 100, "application did not draw its terminal UI"
    assert "AddressSanitizer" not in error_text and "runtime error:" not in error_text, error_text
    print("Terminal startup and F1 shutdown passed")
