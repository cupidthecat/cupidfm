#!/usr/bin/env python3
"""Exercise literal jump results, selection narrowing, cancellation, and the real fuzzy finder."""
import fcntl
import json
import os
from pathlib import Path
import pty
import select
import shutil
import struct
import subprocess
import tempfile
import termios
import time

binary = Path(os.environ.get("CUPIDFM_TEST_BINARY", Path(__file__).resolve().parents[1] / "cupidfm"))
real_fzf = shutil.which("fzf")
real_zoxide = shutil.which("zoxide")
assert real_fzf and real_zoxide, "fzf and zoxide are required for navigation tests"
with tempfile.TemporaryDirectory(prefix="cupidfm-jump-") as temporary:
    root = Path(temporary); home = root / "home"; home.mkdir()
    source = root / "files"; source.mkdir()
    child = source / "child"; child.mkdir()
    history = root / "history\nplace"; history.mkdir()
    names = ["a ' $(not-a-command).txt", "b\nnewline.txt"]
    for name in names: (source / name).write_text(name)
    (source / ".hidden").write_text("hidden")
    (source / "broken").symlink_to(root / "does-not-exist")
    (home / ".cupidfmrc").write_text("keymap=vim\n")
    plugins = home / ".cupidfm/plugins"; plugins.mkdir(parents=True)
    (plugins / "inspect.cs").write_text(r'''
let checks = 0;
fn on_load() { write_file(getenv("CUPIDFM_JUMP_ROOT") + "/ready", "ready"); }
fn on_key(key) {
    if (key == "!") {
        checks = checks + 1;
        let base = getenv("CUPIDFM_JUMP_ROOT") + "/check-" + to_str(checks);
        write_file(base + "-cwd", fm.cwd());
        write_file(base + "-name", fm.selected_name());
        write_file(base + "-count", to_str(len(fm.selected_paths())));
        write_file(base + "-done", "done");
        return true;
    }
    return false;
}
''')
    programs = root / "bin"; programs.mkdir()
    for program in ("fzf", "zoxide"):
        script = programs / program
        script.write_text('''#!/usr/bin/python3
import json,os,sys
from pathlib import Path
root=Path(os.environ["CUPIDFM_JUMP_ROOT"]); name=Path(sys.argv[0]).name
data=sys.stdin.buffer.read() if name=="fzf" and not sys.stdin.isatty() else b""
(root/(name+"-args.json")).write_text(json.dumps({"argv":sys.argv[1:],"input":list(data),"cwd":str(Path.cwd())}))
sys.stdout.buffer.write((root/(name+"-result")).read_bytes())
sys.exit(int((root/(name+"-code")).read_text()) if (root/(name+"-code")).exists() else 0)
''')
        script.chmod(0o755)
    primary, secondary = pty.openpty()
    fcntl.ioctl(secondary, termios.TIOCSWINSZ, struct.pack("HHHH", 32, 120, 0, 0))
    env = dict(os.environ, TERM="xterm-256color", HOME=str(home), CUPIDFM_JUMP_ROOT=str(root),
               PATH=str(programs)+os.pathsep+os.environ["PATH"])
    with tempfile.TemporaryFile() as errors:
        process = subprocess.Popen([str(binary), str(source)], env=env, stdin=secondary,
                                   stdout=secondary, stderr=errors, start_new_session=True)
        os.close(secondary); output = bytearray(); checks = 0
        def wait_for(predicate, timeout=15):
            deadline=time.monotonic()+timeout
            while time.monotonic()<deadline:
                if predicate(): return
                if process.poll() is not None: break
                ready,_,_=select.select([primary],[],[],0.02)
                if ready:
                    try: output.extend(os.read(primary,65536))
                    except OSError: break
            errors.seek(0)
            raise AssertionError((process.poll(), errors.read().decode(errors="replace"), bytes(output[-2000:])))
        def snapshot(keys=b""):
            global checks
            checks+=1; base=root/f"check-{checks}"
            os.write(primary, keys+b"!")
            wait_for(Path(str(base)+"-done").exists)
            return {part:Path(str(base)+"-"+part).read_text() for part in ("cwd","name","count")}
        def fzf_result(paths):
            (root/"fzf-result").write_bytes(b"".join(os.fsencode(path)+b"\0" for path in paths))
        try:
            wait_for((root/"ready").exists)
            fzf_result([names[0]])
            assert snapshot(b"z")["name"] == names[0]
            fzf_result(["child"])
            assert snapshot(b"z")["cwd"] == str(child)
            (root/"zoxide-result").write_bytes(os.fsencode(history)+b"\n")
            assert snapshot(b"Z")["cwd"] == str(history)
            arguments=json.loads((root/"zoxide-args.json").read_text())
            assert arguments["argv"] == ["query","-i","--exclude",str(child)]
            snapshot(b"g "+str(source).encode()+b"\n")
            assert snapshot(b"\x01")["count"] == "4"
            fzf_result([str(source/name) for name in names])
            assert snapshot(b"z")["count"] == "2"
            arguments=json.loads((root/"fzf-args.json").read_text())
            assert "--read0" in arguments["argv"]
            assert os.fsencode(source/names[1])+b"\0" in bytes(arguments["input"])
            fzf_result(["child"])
            state=snapshot(b"z")
            assert state["cwd"] == str(source) and state["name"] == "child" and state["count"] == "2"
            fzf_result(["child","broken"])
            assert snapshot(b"z")["count"] == "1", "selected_paths falls back to the hovered entry"
            fzf_result(["child"])
            assert snapshot(b"z")["cwd"] == str(child), "no selection remains, so the directory opens"
            assert snapshot(b"h")["name"] == "child"
            (root/"fzf-code").write_text("130")
            assert snapshot(b"z")["name"] == "child"
            (root/"fzf-code").unlink()
            (root/"fzf-result").write_bytes(b"missing-NUL")
            assert snapshot(b"z")["name"] == "child"
            fzf_result(names)
            assert snapshot(b"z")["count"] == "2"
            fzf_result([".hidden"])
            state=snapshot(b"z")
            assert state["cwd"] == str(source) and state["name"] == ".hidden", state
            os.write(primary,b"q");process.wait(timeout=10)
            errors.seek(0); error=errors.read().decode(errors="replace")
            assert process.returncode==0 and "AddressSanitizer" not in error and "runtime error:" not in error,error
        finally:
            if process.poll() is None: process.kill();process.wait()
            os.close(primary)

    if real_fzf:
        # The real program consumes the NUL-delimited selected paths in filter mode.
        (root/"ready").unlink()
        for receipt in root.glob("check-*"): receipt.unlink()
        primary,secondary=pty.openpty()
        fcntl.ioctl(secondary,termios.TIOCSWINSZ,struct.pack("HHHH",32,120,0,0))
        env["PATH"]=os.environ["PATH"]
        env["FZF_DEFAULT_OPTS"]="--filter="
        env.pop("FZF_DEFAULT_OPTS_FILE", None)
        env["_ZO_FZF_OPTS"]="--filter=real-history"
        env["_ZO_DATA_DIR"]=str(root/"zoxide-data")
        env["SHELL"]="sh"
        real_history=root/"real-history";real_history.mkdir()
        subprocess.run([real_zoxide,"add",str(real_history)],env=env,check=True,capture_output=True)
        with tempfile.TemporaryFile() as errors:
            process=subprocess.Popen([str(binary),str(source)],env=env,stdin=secondary,stdout=secondary,
                                     stderr=errors,start_new_session=True)
            os.close(secondary);output=bytearray();checks=0
            try:
                wait_for((root/"ready").exists)
                assert snapshot(b"\x01")["count"]=="4"
                assert snapshot(b"z")["count"]=="1", "selected_paths falls back to the hovered entry"
                assert snapshot(b"\x12")["count"]=="4", "inverting after narrowing selects all entries"
                assert snapshot(b"Z")["cwd"]==str(real_history)
                os.write(primary,b"q");process.wait(timeout=10)
                errors.seek(0);error=errors.read().decode(errors="replace")
                assert process.returncode==0 and "AddressSanitizer" not in error and "runtime error:" not in error,error
            finally:
                if process.poll() is None: process.kill();process.wait()
                os.close(primary)
print("File/directory/history jumps, NUL paths, selection narrowing, broken links, cancellation, and real navigation tools passed")
