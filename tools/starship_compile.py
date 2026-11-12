"""Audit Starship's existing native C core against the ESP32-P4 compiler.

Desktop Engine.cpp/SDL/GUI backends are not part of this audit. No game is linked.
"""
from pathlib import Path
import concurrent.futures
import json
import os
import re
import shutil
import subprocess

ROOT=Path(__file__).resolve().parents[1]
SOURCE=ROOT/"third_party/starship"
LUS=SOURCE/"libultraship"
OUT=ROOT/"build-starship"
CC=Path(os.environ.get("SF64_CC") or shutil.which("riscv32-esp-elf-gcc") or r"C:/Espressif/tools/riscv32-esp-elf/esp-14.2.0_20260121/riscv32-esp-elf/bin/riscv32-esp-elf-gcc.exe")
INCLUDES=[OUT/"include",OUT/"include/libultraship",SOURCE,SOURCE/"include",SOURCE/"src",LUS,
          LUS/"src",LUS/"src/graphic",LUS/"src/utils/binarytools"]
FLAGS=["-c","-O1","-std=gnu99","-march=rv32imafc_zicsr_zifencei","-mabi=ilp32f",
       "-fno-strict-aliasing","-ffunction-sections","-fdata-sections",
       "-DVERSION_US=1","-DF3DEX_GBI=1","-D_LANGUAGE_C","-DNON_MATCHING=1","-DNON_EQUIVALENT=1",
       "-DAVOID_UB=1","-DGBI_FLOATS=1","-Wno-error=int-conversion","-Wno-error=incompatible-pointer-types"]


def main():
    OUT.mkdir(exist_ok=True)
    shutil.copytree(LUS/"include",OUT/"include",dirs_exist_ok=True)
    # Newlib on RV32 defines int32_t as long, while this upstream revision mixes
    # int32_t and plain int in declarations. Keep fixed-width aliases consistent.
    types=OUT/"include/libultraship/libultra/types.h"
    text=types.read_text(encoding="utf8")
    text=text.replace("typedef signed int s32;","typedef int32_t s32;")
    text=text.replace("typedef unsigned int u32;","typedef uint32_t u32;")
    types.write_text(text,encoding="utf8")
    abi=OUT/"include/libultraship/libultra/abi.h"
    text=abi.read_text(encoding="utf8").replace("typedef unsigned int u32;","#include <stdint.h>\ntypedef uint32_t u32;")
    abi.write_text(text,encoding="utf8")
    os_header=OUT/"include/libultraship/libultra/os.h"
    text=os_header.read_text(encoding="utf8")
    text=text.replace("osEepromLongRead(OSMesgQueue*, u8, u8*, int)","osEepromLongRead(OSMesgQueue*, u8, u8*, s32)")
    text=text.replace("osEepromLongWrite(OSMesgQueue*, u8, u8*, int)","osEepromLongWrite(OSMesgQueue*, u8, u8*, s32)")
    os_header.write_text(text,encoding="utf8")
    paths=[]
    for folder in ("engine","audio","overlays","sys"):
        paths.extend((SOURCE/"src"/folder).rglob("*.c"))
    paths.extend(SOURCE/f"src/{name}.c" for name in ("buffers","dmatable","libc_math64","1FA60"))
    paths=[path for path in paths if path.exists() and not path.name.endswith(".inc.c")
           and path.name not in ("sys_fault.c","sys_timer.c")]
    def compile_one(source):
        relative=source.relative_to(SOURCE)
        obj=OUT/relative.with_suffix(".o")
        obj.parent.mkdir(parents=True,exist_ok=True)
        result=subprocess.run([str(CC),*FLAGS,*("-I"+str(path) for path in INCLUDES),
                               "-I"+str(source.parent),str(source),"-o",str(obj)],capture_output=True,text=True,errors="replace")
        log=obj.with_suffix(".log")
        log.write_text(result.stdout+result.stderr,encoding="utf8")
        return {"source":str(relative),"compiled":result.returncode==0,"log":str(log.relative_to(ROOT)),
                "errors":[line for line in result.stderr.splitlines() if "error:" in line]}
    with concurrent.futures.ThreadPoolExecutor(max_workers=6) as pool:
        result=list(pool.map(compile_one,sorted(paths)))
    report={"compiled":sum(item["compiled"] for item in result),"total":len(result),
            "linked":False,"running":False,"files":result}
    if report["compiled"]==report["total"]:
        included=set()
        for source in paths:
            for name in re.findall(r'#include\s+"([^"]+\.c)"',source.read_text(encoding="utf8",errors="replace")):
                included.add((source.parent/name).resolve())
        objects=[OUT/path.relative_to(SOURCE).with_suffix(".o") for path in sorted(paths)
                 if path.resolve() not in included]
        linked=subprocess.run([str(CC.with_name("riscv32-esp-elf-ld.exe")),"-r","-o",
            str(OUT/"starship-core.o"),*map(str,objects)],capture_output=True,text=True,errors="replace")
        (OUT/"partial-link.log").write_text(linked.stdout+linked.stderr)
        report["partial_link"]=linked.returncode==0
        if not linked.returncode:
            symbols=subprocess.run([str(CC.with_name("riscv32-esp-elf-nm.exe")),"-u",str(OUT/"starship-core.o")],
                                   capture_output=True,text=True).stdout
            report["unresolved"]=sorted(set(re.findall(r"\bU\s+(\S+)",symbols)))
            report["unresolved_count"]=len(report["unresolved"])
            archived=subprocess.run([str(CC.with_name("riscv32-esp-elf-ar.exe")),"rcs",
                                     str(OUT/"libstarship-core.a"),str(OUT/"starship-core.o")])
            report["archive_created"]=archived.returncode==0
    (OUT/"compile-report.json").write_text(json.dumps(report,indent=2))
    print(f"Starship native C core: {report['compiled']}/{report['total']} compile; game not linked.")
    if report.get("partial_link"):
        print("Partial link passed; unresolved platform/resource/port symbols:",report["unresolved_count"])
    seen=set()
    for item in result:
        for error in item["errors"]:
            message=re.sub(r"^.*?:\d+:\d+: ","",error)
            if message not in seen:
                seen.add(message)
                print(error)
            if len(seen)>=15: return


if __name__=="__main__": main()
