"""Decode a complete RGB565 framebuffer readback from an actual board log."""
import argparse
from pathlib import Path
import re
import struct
import zlib

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument("log",type=Path)
parser.add_argument("--output",type=Path,required=True)
parser.add_argument("--state",type=int)
parser.add_argument("--scale",type=int,default=4,choices=range(1,9))
args=parser.parse_args()
frames=[]
current=None
for line in args.log.read_text(encoding="utf8",errors="replace").splitlines():
    header=re.fullmatch(r"SF64_FRAME_BEGIN (\d+) (\d+)(?: state=(\d+))?",line)
    if header:
        current=(int(header[1]),int(header[2]),int(header[3]) if header[3] else None,[])
    elif line.startswith("SF64_ROW ") and current:
        row=line.removeprefix("SF64_ROW ")
        if re.fullmatch(r"[0-9a-f]{"+str(current[0]*4)+"}",row): current[3].append(row)
    elif line=="SF64_FRAME_END" and current:
        if len(current[3])==current[1] and (args.state is None or current[2]==args.state): frames.append(current)
        current=None
if not frames: raise SystemExit("No complete framebuffer found for the requested state")
width,height,state,rows=frames[-1]
raw=bytearray()
for row in rows:
    rgb=bytearray()
    for x in range(width):
        value=int(row[x*4:x*4+4],16)
        rgb.extend(bytes((((value>>11)&31)*255//31,((value>>5)&63)*255//63,(value&31)*255//31))*args.scale)
    raw.extend((b"\0"+rgb)*args.scale)
def chunk(kind,data):
    return struct.pack(">I",len(data))+kind+data+struct.pack(">I",zlib.crc32(kind+data))
png=b"\x89PNG\r\n\x1a\n"
png+=chunk(b"IHDR",struct.pack(">IIBBBBB",width*args.scale,height*args.scale,8,2,0,0,0))
png+=chunk(b"IDAT",zlib.compress(raw))+chunk(b"IEND",b"")
args.output.parent.mkdir(parents=True,exist_ok=True)
args.output.write_bytes(png)
print(f"Decoded actual board framebuffer: state={state}, {width}x{height}, output={args.output}")
