#!/usr/bin/env python3
"""Check CLI failures and terminal startup/shutdown in isolated directories."""
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

def terminal(home, cwd, arguments, marker):
    primary, secondary = pty.openpty()
    fcntl.ioctl(secondary, termios.TIOCSWINSZ, struct.pack("HHHH", 30, 100, 0, 0))
    env = dict(os.environ, TERM="xterm-256color", HOME=str(home), XDG_CONFIG_HOME=str(home / ".config"))
    with (home / "stderr").open("wb") as errors:
        process = subprocess.Popen([str(binary), *arguments], cwd=cwd, env=env,
                                   stdin=secondary, stdout=secondary, stderr=errors, start_new_session=True)
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
    assert process.returncode == 0, (arguments, process.returncode, error_text)
    assert marker.encode() in output, (arguments, "starting directory contents were not drawn")
    assert b"PLUGIN_ALIASES_OK" in output, (arguments, "plugin compatibility calls did not finish")
    assert "AddressSanitizer" not in error_text and "runtime error:" not in error_text, error_text

with tempfile.TemporaryDirectory(prefix="cupidfm-terminal-") as directory:
    base = Path(directory)
    home = base / "home"; home.mkdir(); (home / ".config").mkdir()
    (home / ".cupidfmrc").write_text("# Use default bindings in this isolated test\n")
    plugins = home / ".cupidfm" / "plugins"; plugins.mkdir(parents=True)
    (plugins / "legacy.cs").write_text('''fn on_load() {
        assert(fm.get_cwd() == fm.cwd(), "cwd alias");
        assert(fm.get_selected() == fm.selected_name(), "selected name alias");
        assert(fm.get_selected_path() == fm.selected_path(), "selected path alias");
        assert(len(fm.get_files()) == len(fm.entries()), "entries alias");
        assert(fm.editor_is_open() == fm.editor_active(), "editor active alias");
        assert(fm.editor_insert("") == fm.editor_insert_text(""), "editor insert alias");
        fm.set_cwd(fm.cwd());
        fm.refresh();
        fm.notify("PLUGIN_ALIASES_OK");
    }''')
    working = base / "working"; working.mkdir()
    (working / "default-only.txt").write_text("default directory\n")
    target = base / "start folder"; target.mkdir()
    (target / "target-only.txt").write_text("requested directory\n")
    dashed = working / "-dash-start"; dashed.mkdir()
    (dashed / "dash-only.txt").write_text("directory beginning with a dash\n")
    plain_env = dict(os.environ, HOME=str(home)); plain_env.pop("TERM", None)
    for args, status in ((["--help"], 0), (["-h"], 0), (["missing-directory"], 1),
                         ([str(working / "default-only.txt")], 1), (["--unknown"], 2), (["one", "two"], 2)):
        result = subprocess.run([str(binary), *args], cwd=working, env=plain_env, capture_output=True, text=True, timeout=5)
        assert result.returncode == status, (args, result.returncode, result.stderr)
        assert "\x1b[" not in result.stdout + result.stderr, "CLI validation initialized the terminal"
        if status == 0: assert "Usage:" in result.stdout
    for args, marker in (([], "default-only.txt"), (["../start folder"], "target-only.txt"),
                         ([str(target)], "target-only.txt"), (["--", "-dash-start"], "dash-only.txt")):
        terminal(home, working, args, marker)
print("CLI startup directories, plugin compatibility calls, and F1 shutdown passed")
