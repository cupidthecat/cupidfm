#!/usr/bin/env python3
"""Measure image-preview output and CPU in a PTY; this does not measure a terminal GPU."""
import argparse
import fcntl
import json
import os
from pathlib import Path
import pty
import resource
import select
import statistics
import struct
import subprocess
import tempfile
import termios
import time

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--baseline", type=Path)
parser.add_argument("--current", type=Path, default=Path(__file__).resolve().parents[1] / "cupidfm")
parser.add_argument("--runs", type=int, default=3)
parser.add_argument("--protocols", default="kitty,ansi,sixel")
options = parser.parse_args()
protocols = options.protocols.split(",")
if options.runs < 1 or any(value not in ("kitty", "ansi", "sixel") for value in protocols):
    parser.error("positive run count and kitty/ansi/sixel protocols required")


def run(binary, directory, home, protocol):
    primary, secondary = pty.openpty()
    fcntl.ioctl(secondary, termios.TIOCSWINSZ, struct.pack("HHHH", 30, 100, 0, 0))
    environment = dict(os.environ)
    for name in ("KITTY_WINDOW_ID", "WT_SESSION", "VTE_VERSION", "KONSOLE_VERSION", "COLORTERM", "TERM_PROGRAM"):
        environment.pop(name, None)
    environment.update(HOME=str(home), XDG_CONFIG_HOME=str(home / ".config"), LANG="C.UTF-8")
    environment["TERM"] = "screen-256color" if protocol == "ansi" else "xterm-256color"
    if protocol == "kitty": environment["KITTY_WINDOW_ID"] = "1"
    if protocol == "ansi": environment["COLORTERM"] = "truecolor"
    if protocol == "sixel": environment["WT_SESSION"] = "benchmark"
    marker = {"kitty": b"\x1b_Gf=32", "ansi": b"\x1b[48;2;", "sixel": b"\x1bP"}[protocol]
    before = resource.getrusage(resource.RUSAGE_CHILDREN)
    start = time.monotonic()
    ready = None
    sent = 0
    output_bytes = 0
    tail = b""
    with tempfile.TemporaryFile() as errors:
        process = subprocess.Popen([str(binary), str(directory)], cwd=directory, env=environment,
                                   stdin=secondary, stdout=secondary, stderr=errors, start_new_session=True)
        os.close(secondary)
        try:
            while process.poll() is None:
                elapsed = time.monotonic() - start
                if elapsed > 15: raise TimeoutError("image workload timed out")
                readable, _, _ = select.select([primary], [], [], 0.01)
                if readable:
                    try: chunk = os.read(primary, 65536)
                    except OSError: break
                    output_bytes += len(chunk)
                    tail = (tail + chunk)[-131072:]
                    if ready is None and marker in tail: ready = time.monotonic() - start
                if ready is not None:
                    age = elapsed - ready
                    if sent == 0 and age >= 0.35: os.write(primary, b"\x1bOB"); sent = 1
                    elif sent == 1 and age >= 0.70: os.write(primary, b"\x1bOA"); sent = 2
                    elif sent == 2 and age >= 1.10: os.write(primary, b"\x1bOP"); sent = 3
            process.wait(timeout=10)
        finally:
            if process.poll() is None: process.kill(); process.wait()
            os.close(primary)
        errors.seek(0)
        error = errors.read().decode(errors="replace")
    if process.returncode or ready is None or sent != 3:
        raise RuntimeError((protocol, process.returncode, ready, sent, error))
    if any(word in error for word in ("AddressSanitizer", "LeakSanitizer", "runtime error:")):
        raise RuntimeError(error)
    after = resource.getrusage(resource.RUSAGE_CHILDREN)
    return {"first_protocol_output_ms": ready * 1000,
            "cpu_ms": ((after.ru_utime-before.ru_utime)+(after.ru_stime-before.ru_stime))*1000,
            "wall_ms": (time.monotonic()-start)*1000, "output_bytes": output_bytes}


with tempfile.TemporaryDirectory(prefix="cupidfm-image-benchmark-") as temporary:
    root = Path(temporary)
    directory = root / "images"; directory.mkdir()
    home = root / "home"; home.mkdir(); (home / ".config").mkdir()
    (home / ".cupidfmrc").write_text("# Default bindings\n")
    (home / ".cupidfm/plugins").mkdir(parents=True)
    width, height = 2048, 1536
    row = bytes(value for x in range(width) for value in ((x*13)&255, (x*7)&255, (x*3)&255))
    header = b"BM" + struct.pack("<IHHI", 54+width*height*3, 0, 0, 54)
    header += struct.pack("<IiiHHIIiiII", 40, width, height, 1, 24, 0, width*height*3, 2835, 2835, 0, 0)
    for name in ("a.bmp", "b.bmp"):
        with (directory / name).open("wb") as file:
            file.write(header)
            for y in range(height): file.write(bytes((value+y*5)&255 for value in row))
    time.sleep(2.1)
    binaries = [("current", options.current.resolve())]
    if options.baseline: binaries.insert(0, ("baseline", options.baseline.resolve()))
    result = {"scope": "30x100 PTY, isolated default config, 2048x1536 BMPs, first protocol bytes then select next/back and exit after 1.1 seconds. Warm file caches. Protocol features simulated via environment. No terminal display/GPU latency or FPS claim.", "workloads": {}}
    for name, binary in binaries:
        for protocol in protocols:
            runs = [run(binary, directory, home, protocol) for _ in range(options.runs)]
            result["workloads"][name+"_"+protocol] = {"runs": runs, "median": {key: statistics.median(run[key] for run in runs) for key in runs[0]}}
    print(json.dumps(result, indent=2))
