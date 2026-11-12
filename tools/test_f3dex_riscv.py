"""Execute the real C++ F3DEX subset on a simulated RV32IM CPU, not an ESP32 board."""
from pathlib import Path
import struct
import sys
import json
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/"build-tests/python"))
from elftools.elf.elffile import ELFFile
from unicorn import Uc, UC_ARCH_RISCV, UC_MODE_RISCV32
from unicorn.riscv_const import UC_RISCV_REG_A0, UC_RISCV_REG_A1, UC_RISCV_REG_A2, UC_RISCV_REG_A3, UC_RISCV_REG_RA, UC_RISCV_REG_SP, UC_RISCV_REG_PC

cpu=Uc(UC_ARCH_RISCV,UC_MODE_RISCV32)
cpu.mem_map(0,16*1024*1024)
with (ROOT/"build-tests/f3dex-test.elf").open("rb") as stream:
    elf=ELFFile(stream)
    for segment in elf.iter_segments():
        if segment["p_type"]=="PT_LOAD":
            cpu.mem_write(segment["p_vaddr"],segment.data())
    symbols={symbol.name:symbol["st_value"] for symbol in elf.get_section_by_name(".symtab").iter_symbols()}


def call(name,*args):
    cpu.reg_write(UC_RISCV_REG_SP,0xe00000)
    cpu.reg_write(UC_RISCV_REG_RA,0xf00000)
    for register,value in zip((UC_RISCV_REG_A0,UC_RISCV_REG_A1,UC_RISCV_REG_A2,UC_RISCV_REG_A3),args):
        cpu.reg_write(register,value & 0xffffffff)
    cpu.emu_start(symbols[name],0xf00000,count=200000000)
    assert cpu.reg_read(UC_RISCV_REG_PC)==0xf00000, f"RV32IM instruction budget exceeded in {name}"
    return cpu.reg_read(UC_RISCV_REG_A0)


buffer=call("test_input")
output=call("test_triangles")


def run(data,address=0x03000000):
    cpu.mem_write(buffer,bytes(128*1024))
    cpu.mem_write(buffer,data)
    return call("test_run",len(data),address)


def command(w0,w1=0):
    return struct.pack(">II",w0,w1)


raw=(ROOT/"assets/local/segments/ast_arwing.bin").read_bytes()
assert run(raw,0x03009b60)==0
assert call("test_count")==70
report=json.loads((ROOT/"assets/local/arwing_body.json").read_text())
assert call("test_commands")==sum(report["opcodes"].values())
expected=(ROOT/"assets/local/arwing_body_triangles.bin").read_bytes()
assert bytes(cpu.mem_read(output,70*18))==expected, "Runtime/offline triangle stream mismatch"
assert run(command(0xb8000000))==0
assert call("test_count")==0
assert run(command(0xbf000000,0x000204))==3
assert run(command(0x04000000,0x03000000))==2
assert run(command(0x01000000))==4
assert run(command(0x06000000,0x03000000))==5
assert run(command(0xb8000000),0x04000000)==1
assert run(bytes(8*8193))==6
print("RV32IM simulation: ROM 70-triangle exact comparison + 7 edge/error cases passed.")
call("test_input_step",100,-100,0xc000,1)
assert call("test_input_held")==0xc000
assert call("test_input_pressed")==0xc000
assert call("test_input_x")==80
assert call("test_input_y")==(-80 & 0xffffffff)
call("test_input_step",20,30,0xc000,1)
assert call("test_input_pressed")==0
assert call("test_input_released")==0
call("test_input_step",0,0,0x8000,1)
assert call("test_input_released")==0x4000
call("test_input_step",30,20,0x8000,0)
assert call("test_input_held")==0
assert call("test_input_released")==0x8000
assert call("test_input_x")==0
assert call("test_input_y")==0
call("test_input_step",0,0,0x8000,1)
call("test_input_disconnect")
assert call("test_input_released")==0x8000
assert call("test_input_held")==0
print("RV32IM input core: clamping, hold/press/release, failed reads and driver disconnect passed.")
call("test_input_background",1)
call("test_input_report",0x1000)
call("test_input_report",0)  # Entire tap occurs between game frames.
assert call("test_input_snapshot")==0  # Sampling must not change a delivered frame.
call("test_input_consume")
assert call("test_input_pressed")==0x1000
assert call("test_input_snapshot")==0x1000
call("test_input_report",0x8000)
assert call("test_input_snapshot")==0x1000
call("test_input_consume")
assert call("test_input_pressed")==0x8000
assert call("test_input_released")==0x1000
call("test_input_report",0)
call("test_input_consume")
assert call("test_input_released")==0x8000
call("test_input_background",0)
print("RV32IM input sampling: short Start tap latched, stable game snapshot, A and release transitions passed.")
# Packed RDP alpha mux fixtures: TEXEL0*PRIMITIVE, PRIMITIVE, SHADE,
# and a two-cycle TEXEL0*SHADE followed by COMBINED*PRIMITIVE.
assert call("test_rdp_alpha",0xfc001600,0x002c7e3f,0xff8000ff,0)==128
assert call("test_rdp_alpha",0xfc001600,0x002c7e3f,0x808000ff,0)==64
assert call("test_rdp_alpha",0xfc001600,0x002c7e3f,0x008000ff,0)==0
assert call("test_rdp_alpha",0xfc007e00,0x00fc763b,0x008000ff,0)==128
assert call("test_rdp_alpha",0xfc007e00,0x00fc783c,0xff800040,0)==64
assert call("test_rdp_alpha",0xfc001800,0x000c7e3f,0xff8000a0,1)==80
assert call("test_rdp_alpha",0xfc001800,0x000c7e3f,0x808000a0,1)==40
assert call("test_rdp_alpha",0xfc001c00,0x00007e00,0xff000000,64<<8)==64
print("RV32IM RDP alpha: texture/primitive/shade, transparent texels, two-cycle fade and LOD mux passed.")
packed_pointer=call("test_mio_source")
decoded_pointer=call("test_mio_dest")
rom=(ROOT/"assets/local/baserom.us.z64").read_bytes()
segments=json.loads((ROOT/"assets/local/segments/segments.json").read_text())["segments"]
decoded_count=0
for segment in segments:
    if not segment["compressed"]:
        continue
    encoded=rom[segment["rom_offset"]:segment["rom_offset"]+segment["rom_size"]]
    assert len(encoded)<=2*1024*1024 and segment["size"]<=4*1024*1024
    cpu.mem_write(packed_pointer,encoded)
    assert call("test_mio_decode",len(encoded),segment["size"])==1, segment["name"]
    expected_segment=(ROOT/f"assets/local/segments/{segment['name']}.bin").read_bytes()
    assert bytes(cpu.mem_read(decoded_pointer,segment["size"]))==expected_segment, segment["name"]
    decoded_count+=1
bad=b"MIO0"+struct.pack(">III",3,17,19)+bytes(3)
cpu.mem_write(packed_pointer,bad)
assert call("test_mio_decode",len(bad),3)==0
assert call("test_mio_decode",5,3)==0
print(f"RV32IM MIO0: all {decoded_count} compressed ROM segments exactly match Python extraction; invalid data rejected.")
assert call("test_pcm",0,1,32000)==1
assert call("test_pcm",1,1,32000)==0
assert call("test_pcm_sum")==0xffffffff
assert call("test_pcm",2,1,32000)==3
assert call("test_pcm",1,0,32000)==2
assert call("test_pcm",1,1,0)==2
print("RV32IM PCM interface: stereo samples, no sink, backpressure and invalid input passed; no mixer/audio hardware tested.")
print("This validates interpreter execution; it is not ESP32-P4 hardware or game testing.")
