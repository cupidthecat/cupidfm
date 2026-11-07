#!/usr/bin/env python3
"""Compare browser idle CPU and queued-key latency in isolated PTYs."""
import fcntl,json,os,pty,resource,select,struct,subprocess,tempfile,termios,time
from pathlib import Path
import argparse
p=argparse.ArgumentParser(description=__doc__);p.add_argument("binaries",nargs="+",type=Path);p.add_argument("--runs",type=int,default=3)
a=p.parse_args()
with tempfile.TemporaryDirectory(prefix="cupidfm-perf-") as tmp:
    root=Path(tmp);home=root/"home";home.mkdir();files=root/"files";files.mkdir()
    for i in range(5000): (files/f"entry-{i:05}.bin").touch()
    (home/".cupidfmrc").write_text("keymap=vim\n")
    plugins=home/".cupidfm/plugins";plugins.mkdir(parents=True)
    (plugins/"ready.cs").write_text('fn on_load(){write_file(getenv("PERF_READY"),"ready");} fn on_key(k){if(k=="!"){write_file(getenv("PERF_DONE"),to_str(fm.cursor()));return true;}return false;}')
    for binary in a.binaries:
        samples=[]
        for run in range(a.runs):
            ready=root/"ready";done=root/"done";ready.unlink(missing_ok=True);done.unlink(missing_ok=True)
            primary,secondary=pty.openpty();fcntl.ioctl(secondary,termios.TIOCSWINSZ,struct.pack("HHHH",32,120,0,0))
            env=dict(os.environ,TERM="xterm-256color",HOME=str(home),PERF_READY=str(ready),PERF_DONE=str(done),CUPIDFM_SYNTAX_DIR=str(Path(__file__).resolve().parents[1]/"syntax"))
            before=resource.getrusage(resource.RUSAGE_CHILDREN)
            with tempfile.TemporaryFile() as errors:
                child=subprocess.Popen([str(binary.resolve()),str(files)],stdin=secondary,stdout=secondary,stderr=errors,env=env,start_new_session=True)
                os.close(secondary)
                def wait(predicate,timeout=15):
                    end=time.monotonic()+timeout
                    while time.monotonic()<end:
                        if predicate():return
                        if child.poll() is not None:break
                        if select.select([primary],[],[],.01)[0]:
                            try:os.read(primary,65536)
                            except OSError:break
                    raise AssertionError("browser did not respond")
                try:
                    wait(ready.exists);end=time.monotonic()+2
                    wait(lambda:time.monotonic()>=end)
                    start=time.monotonic();os.write(primary,b"j"*100+b"!");wait(done.exists)
                    latency=time.monotonic()-start;assert done.read_text()=="100"
                    os.write(primary,b"q");child.wait(timeout=5);assert child.returncode==0
                    errors.seek(0);assert b"Sanitizer" not in errors.read()
                finally:
                    if child.poll() is None:child.kill();child.wait()
                    os.close(primary)
            after=resource.getrusage(resource.RUSAGE_CHILDREN)
            samples.append(dict(cpu_seconds=(after.ru_utime+after.ru_stime)-(before.ru_utime+before.ru_stime),key_seconds=latency))
        print(json.dumps(dict(binary=str(binary),samples=samples)))
