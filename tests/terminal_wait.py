"""Terminal EOF and child exit are separate events."""
import errno
import os
import select
import subprocess
import time


def wait_for_terminal(process, primary, predicate, output, errors, timeout=10):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if predicate():
            return
        if process.poll() is not None:
            break
        if not select.select([primary], [], [], 0.01)[0]:
            continue
        try:
            chunk = os.read(primary, 65536)
        except OSError as error:
            if error.errno != errno.EIO:
                raise
            chunk = b""
        if chunk:
            output.extend(chunk)
        else:
            # The terminal can close while shutdown is still finishing.
            try:
                process.wait(timeout=0.05)
            except subprocess.TimeoutExpired:
                pass
    if predicate():
        return
    errors.seek(0)
    raise AssertionError((process.poll(), errors.read(), bytes(output[-3000:])))
