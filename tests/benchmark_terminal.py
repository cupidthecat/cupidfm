#!/usr/bin/env python3
"""Compare optimized terminal builds using isolated generated files."""
from pathlib import Path
import os,subprocess,tempfile,time,pty,fcntl,termios,struct,select,resource,json,statistics
import argparse
parser = argparse.ArgumentParser(description="Measure terminal preview latency and process CPU time")
parser.add_argument("--baseline", type=Path)
parser.add_argument("--current", type=Path, default=Path(__file__).resolve().parents[1] / "cupidfm")
parser.add_argument("--runs", type=int, default=3)
args = parser.parse_args()
if args.runs < 1: parser.error("--runs must be positive")
binaries = [("current", args.current.resolve())]
if args.baseline: binaries.insert(0, ("baseline", args.baseline.resolve()))
def run(binary,home,directory):
 primary,secondary=pty.openpty();fcntl.ioctl(secondary,termios.TIOCSWINSZ,struct.pack('HHHH',30,100,0,0))
 env=dict(os.environ,TERM='xterm-256color',HOME=str(home),XDG_CONFIG_HOME=str(home/'.config'),LANG='C.UTF-8')
 before=resource.getrusage(resource.RUSAGE_CHILDREN);start=time.monotonic();ready=None
 with tempfile.TemporaryFile() as errors:
  process=subprocess.Popen([str(binary),str(directory)],cwd=directory,env=env,stdin=secondary,stdout=secondary,stderr=errors,start_new_session=True);os.close(secondary)
  output=bytearray();sent=False;quit_sent=False
  try:
   while process.poll() is None:
    elapsed=time.monotonic()-start
    if elapsed>15:raise TimeoutError('terminal workload timed out')
    readable,_,_=select.select([primary],[],[],0.02)
    if readable:
     try:output.extend(os.read(primary,65536))
     except OSError:break
     if ready is None and b'PREVIEW_READY_MARKER' in output:ready=time.monotonic()-start
     if len(output)>131072:del output[:-65536]
    if ready is not None and elapsed>ready+0.8 and not sent:
     os.write(primary,b'\t'+b'\x1bOB'*5);sent=True
    if ready is not None and elapsed>ready+1.4 and not quit_sent:
     os.write(primary,b'\x1bOP');quit_sent=True
   process.wait(timeout=10)
  finally:
   if process.poll() is None:process.kill();process.wait()
   os.close(primary)
  errors.seek(0);error=errors.read().decode(errors='replace')
 assert process.returncode==0 and ready is not None and quit_sent,(process.returncode,ready,error)
 after=resource.getrusage(resource.RUSAGE_CHILDREN)
 return {'first_preview_ms':ready*1000,'cpu_ms':((after.ru_utime-before.ru_utime)+(after.ru_stime-before.ru_stime))*1000,'wall_ms':(time.monotonic()-start)*1000}
with tempfile.TemporaryDirectory(prefix='cupidfm-performance-') as temporary:
 base=Path(temporary);home=base/'home';home.mkdir();(home/'.config').mkdir();(home/'.cupidfmrc').write_text('# Default bindings\n')
 (home/'.cupidfm/plugins').mkdir(parents=True)
 large=base/'large';large.mkdir();payload=large/'payload.txt'
 line=b'PREVIEW_READY_MARKER plain text for the file preview benchmark\n'
 block=line*16384
 with payload.open('wb') as stream:
  for _ in range((32*1024*1024+len(block)-1)//len(block)):stream.write(block)
 many=base/'many';many.mkdir()
 for i in range(1000):(many/f'entry-{i:04d}.txt').write_bytes(line)
 results={'scope':'30x100 PTY, isolated default configuration, first preview plus 1.4 seconds with five preview-down key presses; configured runs per workload. Warm host caches, no Git repository or plugins. No FPS claim.','large_file_bytes':payload.stat().st_size,'entries':1000,'workloads':{}}
 time.sleep(2.1) # Compare files whose metadata is outside the conservative recent-change window.
 for name,binary in binaries:
  for workload,directory in [('large_text',large),('1000_entries',many)]:
   runs=[run(binary,home,directory) for _ in range(args.runs)]
   summary={key:statistics.median(r[key] for r in runs) for key in runs[0]}
   results['workloads'][name+'_'+workload]={'median':summary,'runs':runs}
 print(json.dumps(results,indent=2))