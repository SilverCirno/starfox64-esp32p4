"""Prepare a native Starship runtime archive, with unsupported calls trapping.

The trap list is diagnostic and is never a success stub. Boot logs must prove
that no trap was reached before any game-running claim.
"""
from pathlib import Path
import importlib.util
import json
import re
import subprocess
ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location("ship_compile",ROOT/"tools/starship_compile.py")
audit=importlib.util.module_from_spec(spec); spec.loader.exec_module(audit)
OUT=ROOT/"build-starship/runtime"

def command(args):
    result=subprocess.run(list(map(str,args)),capture_output=True,text=True,errors="replace")
    if result.returncode:
        print(result.stdout,result.stderr)
        raise SystemExit(result.returncode)
    return result.stdout

def main():
    OUT.mkdir(parents=True,exist_ok=True)
    cc=audit.CC
    objects=[ROOT/"build-starship/starship-core.o"]
    for name in ("ship_entry","ship_port","ship_events"):
        dest=OUT/f"{name}.o"
        command([cc,*audit.FLAGS,*("-I"+str(p) for p in audit.INCLUDES),"-I"+str(ROOT/"native"),
                 ROOT/f"native/{name}.c","-o",dest])
        objects.append(dest)
    linker=cc.with_name("riscv32-esp-elf-ld.exe")
    combined=OUT/"combined.o"
    command([linker,"-r","-o",combined,*objects])
    undefined=command([cc.with_name("riscv32-esp-elf-nm.exe"),"-u",combined])
    unresolved=sorted(set(re.findall(r"\bU\s+(\S+)",undefined)))
    bridge=OUT/"n64-symbols.o"
    command([cc,"-c","-O2","-std=gnu99","-march=rv32imafc_zicsr_zifencei","-mabi=ilp32f",
             "-D_LANGUAGE_C","-DLANGUAGE_C","-D_MIPS_SZLONG=32","-D_MIPS_SZINT=32","-D_MIPS_SZPTR=32",
             "-DF3DEX_GBI","-DVERSION_US=1","-DNON_MATCHING","-DAVOID_UB","-DNDEBUG",
             "-I"+str(ROOT/"third_party/sf64/include"),"-I"+str(ROOT/"native"),
             ROOT/"native/n64_os.c","-o",bridge])
    supplied=set(re.findall(r"\b[TDBR]\s+(\S+)",command([cc.with_name("riscv32-esp-elf-nm.exe"),bridge])))
    gcc_runtime=command([cc,"-march=rv32imafc_zicsr_zifencei","-mabi=ilp32f","-print-libgcc-file-name"]).strip()
    supplied.update(re.findall(r"\b[TWDBR]\s+(\S+)",command([cc.with_name("riscv32-esp-elf-nm.exe"),"--defined-only",gcc_runtime])))
    supplied.update({"memcpy","memset","memmove","memcmp","strcmp","strcpy","strlen","printf","puts","sprintf","vsprintf",
                     "malloc","free","sinf","cosf","sqrtf","acosf","asinf","atanf","atan2f","ceilf",
                     "floorf","nearbyintf","roundf","tanf","truncf","__udivdi3"})
    supplied.update({"sf64_ship_fail","sf64_ship_resource","sf64_ship_resource_info","sf64_host_time_us","sf64_host_controller"})
    traps=[name for name in unresolved if name not in supplied]
    (OUT/"traps.json").write_text(json.dumps(traps,indent=2))
    lines=['#include "ship_api.h"']
    for name in traps:
        lines.append(f'void {name}(void) {{ sf64_ship_fail("unimplemented: {name}"); }}')
    (OUT/"traps.c").write_text("\n".join(lines)+"\n")
    command([cc,*audit.FLAGS,"-I"+str(ROOT/"native"),OUT/"traps.c","-o",OUT/"traps.o"])
    command([linker,"-r","-o",OUT/"with-traps.o",combined,OUT/"traps.o"])
    sections=command([cc.with_name("riscv32-esp-elf-objdump.exe"),"-h",OUT/"with-traps.o"])
    names=re.findall(r"^\s*\d+\s+(\.(?:bss|sbss)(?:\.\S+)?)\s",sections,re.M)
    options=[]
    for name in names:
        options.extend(["--rename-section",f"{name}=.ext_ram.bss{name[4:] if name.startswith('.bss') else name[5:]}"])
    response=OUT/"objcopy.args"
    response.write_text("\n".join('"'+str(arg).replace('\\','/')+'"'
                                  for arg in [*options,OUT/"with-traps.o",OUT/"starship-runtime.o"])+"\n")
    command([cc.with_name("riscv32-esp-elf-objcopy.exe"),"@"+str(response)])
    command([cc.with_name("riscv32-esp-elf-ar.exe"),"rcs",OUT/"libstarship-runtime.a",OUT/"starship-runtime.o"])
    print("Runtime archive generated. Diagnostic traps:",traps)

if __name__=="__main__": main()
