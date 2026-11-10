#!/usr/bin/env python3
"""A child that closes its terminal before exiting must not fail the wait."""
import os
import pty
import subprocess
import sys
import tempfile

from terminal_wait import wait_for_terminal

primary, secondary = pty.openpty()
with tempfile.TemporaryFile() as errors:
    process = subprocess.Popen(
        [sys.executable, "-c",
         "import os,time; os.close(0); os.close(1); time.sleep(0.2); os._exit(0)"],
        stdin=secondary, stdout=secondary, stderr=errors, start_new_session=True,
    )
    os.close(secondary)
    try:
        wait_for_terminal(process, primary, lambda: process.poll() is not None,
                          bytearray(), errors, timeout=5)
        assert process.returncode == 0
    finally:
        if process.poll() is None:
            process.kill()
            process.wait()
        os.close(primary)
print("Terminal EOF before process exit is handled without losing shutdown status")
