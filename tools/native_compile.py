"""Generate original SF64 asset declarations and cross-compile native game sources.

This is a compile audit, not a linked/runnable game build. Upstream stays intact.
"""
from pathlib import Path
import concurrent.futures
import argparse
import json
import re
import shutil
import subprocess
import struct
import yaml

ROOT=Path(__file__).resolve().parents[1]
UPSTREAM=ROOT/"third_party/sf64"
OUT=ROOT/"build-native"
COMPILER=Path(r"C:\Espressif\tools\riscv32-esp-elf\esp-14.2.0_20260121\riscv32-esp-elf\bin\riscv32-esp-elf-gcc.exe")


def prepare():
    headers=OUT/"include"
    shutil.copytree(UPSTREAM/"include",headers,dirs_exist_ok=True)
    types={
        "GFX":("Gfx",True),"VTX":("Vtx",True),"TEXTURE":("u8",True),
        "BLOB":("u8",True),"SF64:ANIM":("Animation",False),
        "SF64:SKELETON":("Limb*",True),"SF64:LIMB":("Limb",False),
        "SF64:ENVIRONMENT":("Environment",False),
        "SF64:OBJECT_INIT":("ObjectInit",True),"SF64:HITBOX":("f32",True),
        "SF64:SCRIPT":("u16*",True),"SF64:MESSAGE":("u16",True),
        "SF64:TRIANGLE":("Triangle",True),"SF64:COLPOLY":("CollisionPoly",True),
        "SF64:MSG_TABLE":("MsgLookup",True),"VEC3F":("Vec3f",True),
    }
    symbols,unknown={},{}
    for file in sorted((UPSTREAM/"assets/yaml/us/rev1").glob("*.y*ml")):
        doc=yaml.safe_load(file.read_text(encoding="utf-8"))
        lines=["#pragma once"]
        lines.extend(line for line in doc.get(":config",{}).get("header",{}).get("header",[]) if isinstance(line,str))
        for key,item in doc.items():
            if not isinstance(item,dict) or "offset" not in item or key.startswith(":"):
                continue
            kind=item.get("type")
            symbol=item.get("symbol",key)
            ctype,array=types.get(kind,("u8",True))
            if kind=="TEXTURE": ctype=item.get("ctype","u8")
            if kind=="ARRAY": ctype=item.get("array_type",item.get("ctype","u8"))
            if kind not in types and kind!="ARRAY": unknown[kind]=unknown.get(kind,0)+1
            if kind=="SF64:MSG_TABLE": lines.append('#include "sf64mesg.h"')
            count=item.get("count","") if array else ""
            lines.append(f"extern {ctype} {symbol}{f'[{count}]' if array else ''};")
            symbols[symbol]={"address":item["offset"],"kind":kind,"header":file.stem}
            if kind in ("SF64:TRIANGLE","SF64:COLPOLY"):
                mesh_address=item.get("mesh_offset",(item["offset"]+item["count"]*(6 if kind=="SF64:TRIANGLE" else 20)+3)&~3)
                mesh_symbol=item["mesh_symbol"].replace("OFFSET",f"{mesh_address:X}")
                mesh_type="Vec3f" if kind=="SF64:TRIANGLE" else "Vec3s"
                lines.append(f"extern {mesh_type} {mesh_symbol}[];")
                symbols[mesh_symbol]={"address":mesh_address,"kind":mesh_type,"header":file.stem}
                if item.get("mesh_count",1)>1:
                    data=(ROOT/f"assets/local/segments/{file.stem}.bin").read_bytes()
                    base=item["offset"]&0xffffff
                    indices=[]
                    for i in range(item["count"]):
                        indices.extend(struct.unpack_from(">HHH",data,base+i*(6 if kind=="SF64:TRIANGLE" else 20)))
                    mesh_bytes=(max(indices)+1)*(12 if mesh_type=="Vec3f" else 6)
                    for mesh_index in range(1,item["mesh_count"]):
                        extra_address=mesh_address+mesh_index*mesh_bytes
                        extra_symbol=item["mesh_symbol"].replace("OFFSET",f"{extra_address:X}")
                        lines.append(f"extern {mesh_type} {extra_symbol}[];")
                        symbols[extra_symbol]={"address":extra_address,"kind":mesh_type,"header":file.stem}
            if kind=="SF64:MSG_TABLE":
                data=(ROOT/"assets/local/segments/ast_radio.bin").read_bytes()
                position=item["offset"]
                while position+8<=len(data):
                    msg_id,pointer=struct.unpack_from(">iI",data,position)
                    if msg_id<0 or not pointer: break
                    msg_symbol=f"gMsg_ID_{msg_id}"
                    lines.append(f"extern u16 {msg_symbol}[];")
                    symbols[msg_symbol]={"address":pointer,"kind":"SF64:MESSAGE","header":file.stem}
                    position+=8
        for table in doc.get(":config",{}).get("tables",{}):
            lines.append(f"extern u8* {table}[];")
        (headers/"assets"/f"{file.stem}.h").write_text("\n".join(lines)+"\n")
    (OUT/"asset-symbols.json").write_text(json.dumps(symbols,indent=2))
    print("Generated",len(symbols),"asset declarations. Unmapped kinds:",unknown,flush=True)
    return headers


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--headers-only",action="store_true")
    args=parser.parse_args()
    OUT.mkdir(exist_ok=True)
    headers=prepare()
    if args.headers_only: return
    flags=["-c","-O1","-std=gnu99","-fno-builtin","-funsigned-char","-ffunction-sections",
           "-fdata-sections","-fno-strict-aliasing","-Wno-error",
           "-Wno-error=int-conversion","-Wno-error=incompatible-pointer-types",
           "-D_LANGUAGE_C","-DLANGUAGE_C","-D_MIPS_SZLONG=32","-D_MIPS_SZINT=32",
           "-D_MIPS_SZPTR=32","-DF3DEX_GBI","-DVERSION_US=1","-DNON_MATCHING","-DAVOID_UB",
           "-DNDEBUG","-I"+str(headers),"-I"+str(UPSTREAM/"src"),"-I"+str(ROOT/"native")]
    sources=[]
    for group in ("engine","overlays","sys","audio"):
        sources.extend((UPSTREAM/"src"/group).rglob("*.c"))
    sources.extend([UPSTREAM/"src/buffers.c",UPSTREAM/"src/dmatable.c"])
    sources.append(UPSTREAM/"src/libc_math64.c")
    sources.extend(UPSTREAM/f"src/libultra/gu/{name}.c" for name in ("lookat","mtxutil","ortho","perspective"))
    sources=[p for p in sources if not p.name.endswith(".inc.c")]
    sources.append(ROOT/"native/n64_os.c")
    def relative(source):
        return source.relative_to(UPSTREAM/"src") if source.is_relative_to(UPSTREAM/"src") else Path("port")/source.name
    def compile_one(source):
        rel=relative(source)
        target=OUT/"objects"/rel.with_suffix(".o")
        target.parent.mkdir(parents=True,exist_ok=True)
        result=subprocess.run([str(COMPILER),*flags,"-I"+str(source.parent),str(source),"-o",str(target)],
                              capture_output=True,text=True,errors="replace")
        logfile=target.with_suffix(".log")
        logfile.write_text(result.stdout+result.stderr,encoding="utf-8")
        return {"source":str(rel),"ok":result.returncode==0,"log":str(logfile.relative_to(ROOT)),
                "errors":[line for line in result.stderr.splitlines() if "error:" in line or "fatal error:" in line]}
    with concurrent.futures.ThreadPoolExecutor(max_workers=6) as pool:
        results=list(pool.map(compile_one,sorted(sources)))
    report={"linked":False,"runnable":False,"total":len(results),
            "compiled":sum(r["ok"] for r in results),"files":results}
    (OUT/"compile-report.json").write_text(json.dumps(report,indent=2))
    if report["compiled"]==report["total"]:
        included=set()
        for source in sources:
            for name in re.findall(r'#include\s+"([^"]+\.c)"',source.read_text(encoding="utf-8",errors="replace")):
                included.add((source.parent/name).resolve())
        objects=[OUT/"objects"/relative(source).with_suffix(".o")
                 for source in sorted(sources) if source.resolve() not in included]
        linker=COMPILER.with_name("riscv32-esp-elf-ld.exe")
        result=subprocess.run([str(linker),"-r","-o",str(OUT/"sf64-game-relocatable.o"),*map(str,objects)],
                              capture_output=True,text=True,errors="replace")
        (OUT/"partial-link.log").write_text(result.stdout+result.stderr)
        if result.returncode:
            raise RuntimeError("Native partial link failed; see partial-link.log")
        archive=COMPILER.with_name("riscv32-esp-elf-ar.exe")
        result=subprocess.run([str(archive),"rcs",str(OUT/"libsf64-native.a"),str(OUT/"sf64-game-relocatable.o")])
        if result.returncode: raise RuntimeError("Native archive failed")
        report["partial_link"]=True
        report["archive"]="build-native/libsf64-native.a"
        symbols=json.loads((OUT/"asset-symbols.json").read_text())
        aliases=["/* Generated US Rev A asset addresses; not a replacement for endian conversion. */"]
        for name,entry in sorted(symbols.items()):
            aliases.append(f"PROVIDE({name} = 0x{entry['address']:08x});")
        segments=json.loads((ROOT/"assets/local/segments/segments.json").read_text())["segments"]
        for entry in segments:
            name=entry["name"]
            aliases.append(f"PROVIDE({name}_ROM_START = 0x{entry['virtual_offset']:08x});")
            aliases.append(f"PROVIDE({name}_ROM_END = 0x{entry['virtual_offset']+entry['size']:08x});")
        (OUT/"asset-addresses.ld").write_text("\n".join(aliases)+"\n")
        nm=COMPILER.with_name("riscv32-esp-elf-nm.exe")
        output=subprocess.run([str(nm),"-u",str(OUT/"sf64-game-relocatable.o")],capture_output=True,text=True).stdout
        unresolved=sorted(set(re.findall(r"\bU\s+(\S+)",output)))
        alias_names=set(re.findall(r"PROVIDE\((\w+)","\n".join(aliases)))
        remaining=[name for name in unresolved if name not in alias_names]
        (OUT/"platform-dependencies.json").write_text(json.dumps({"unresolved_total":len(unresolved),
            "asset_addresses":len(alias_names),"platform_and_runtime":remaining},indent=2))
        report["platform_and_runtime_count"]=len(remaining)
        (OUT/"compile-report.json").write_text(json.dumps(report,indent=2))
        print("Partial link and native archive passed; remaining runtime symbols:",len(remaining))
    print(f"Compiled {report['compiled']}/{report['total']}; no game linked.")
    seen=set()
    for item in results:
        for error in item["errors"]:
            summary=error.split(": error:")[-1]
            if summary in seen: continue
            seen.add(summary)
            print(error)
            if len(seen)>=25: return


if __name__=="__main__": main()
