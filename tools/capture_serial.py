"""Capture board logs without leaving a serial monitor holding the port."""
import argparse
from pathlib import Path
import time
import serial

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument("port")
parser.add_argument("--seconds",type=float,default=15)
parser.add_argument("--output",type=Path,required=True)
parser.add_argument("--reset",action="store_true")
parser.add_argument("--stop-on-panic",action="store_true",help="Keep three seconds after the first panic/error")
parser.add_argument("--stop-file",type=Path,help="End capture when this marker file appears")
args=parser.parse_args()
data=bytearray()
args.output.parent.mkdir(parents=True,exist_ok=True)
with serial.Serial(args.port,115200,timeout=0.25) as port, args.output.open("wb") as capture:
    if args.reset:
        port.dtr=False
        port.rts=True
        time.sleep(0.12)
        port.rts=False
    end=time.monotonic()+args.seconds
    panic_seen=False
    while time.monotonic()<end:
        if args.stop_file and args.stop_file.exists(): break
        chunk=port.read(4096)
        data.extend(chunk)
        capture.write(chunk)
        capture.flush()
        if args.stop_on_panic and not panic_seen:
            tail=data[-4096:]
            if any(marker in tail for marker in (b"Guru Meditation",b"abort() was called",b"Native game stopped")):
                panic_seen=True
                end=min(end,time.monotonic()+3)
lines=data.decode("utf-8",errors="replace").splitlines()
print("\n".join(line for line in lines if not line.startswith("SF64_ROW "))[-16000:])
