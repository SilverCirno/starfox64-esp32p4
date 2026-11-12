"""Extract only the Arwing body's F3DEX triangle geometry for a TGX model test.

This is not a full display-list interpreter: materials/textures are inventoried,
not reproduced. Encodings follow sf64/include/PR/gbi.h with F3DEX_GBI.
"""
from collections import Counter
import hashlib
import json
from pathlib import Path
import struct

ROOT = Path(__file__).resolve().parents[1]


def export():
    local = ROOT / "assets/local"
    source = local / "segments/ast_arwing.bin"
    data = source.read_bytes()
    manifest = json.loads((local / "segments/segments.json").read_text())
    entry = next(e for e in manifest["segments"] if e["name"] == "ast_arwing")
    if hashlib.sha256(data).hexdigest() != entry["sha256"]:
        raise ValueError("Arwing segment does not match extraction manifest")
    if manifest["rom_sha1"] != "09f0d105f476b00efa5303a3ebc42e60a7753b7a":
        raise ValueError("This model export is currently validated for US Rev A only")
    cache = [None] * 64
    triangles, vertices, lookup = [], [], {}
    opcodes = Counter()
    active = set()

    def offset(address, size):
        position = address & 0xffffff
        if address >> 24 != 3 or position + size > len(data):
            raise ValueError(f"Unmapped segment address {address:08x}")
        return position

    def triangle(word):
        encoded = [(word >> shift) & 255 for shift in (16, 8, 0)]
        if any(v & 1 for v in encoded):
            raise ValueError("Unaligned F3DEX vertex index")
        ids = [v//2 for v in encoded]
        if any(i >= len(cache) or cache[i] is None for i in ids):
            raise ValueError("Triangle references an unloaded vertex")
        result = []
        for i in ids:
            vertex = cache[i]
            if vertex not in lookup:
                lookup[vertex] = len(vertices)
                vertices.append(vertex)
            result.append(lookup[vertex])
        triangles.append(result)

    def display_list(address, depth=0):
        if depth > 16 or address in active:
            raise ValueError("Recursive or excessive display list depth")
        active.add(address)
        pc = offset(address, 8)
        for _ in range(4096):
            if pc+8 > len(data):
                raise ValueError("Display list extends beyond segment")
            w0, w1 = struct.unpack_from(">II", data, pc)
            pc += 8
            op = w0 >> 24
            opcodes[f"0x{op:02x}"] += 1
            if op == 0xb8:  # G_ENDDL
                active.remove(address)
                return
            if op == 4:  # F3DEX G_VTX: v0*2:8 | n:6 | byte_length:10
                start, count = ((w0 >> 16) & 255)//2, (w0 >> 10) & 63
                if not count or start+count > 64 or (w0 & 1023) != count*16-1:
                    raise ValueError("Invalid F3DEX vertex load")
                pos = offset(w1, count*16)
                for i in range(count):
                    cache[start+i] = struct.unpack_from(">hhh", data, pos + i*16)
            elif op == 0xbf:
                triangle(w1)
            elif op == 0xb1:
                triangle(w0)
                triangle(w1)
            elif op == 6:
                display_list(w1, depth+1)
                if (w0 >> 16) & 255:  # G_DL_NOPUSH
                    active.remove(address)
                    return
            elif op in (1, 0xb2, 0xb4):
                raise ValueError(f"Unsupported geometry-changing opcode {op:02x}")
            elif op not in (0, 3, 0xb6, 0xb7, 0xb9, 0xba, 0xbb, 0xbc, 0xbe,
                            0xe6, 0xe7, 0xe8, 0xe9, 0xf0, 0xf2, 0xf3, 0xf5,
                            0xf8, 0xf9, 0xfa, 0xfb, 0xfc, 0xfd):
                raise ValueError(f"Unknown opcode {op:02x}")
        raise ValueError("Display list instruction limit reached")

    display_list(0x03009b60)  # aAwBodyDL in upstream assets/yaml/us/rev1/ast_arwing.yaml
    if not triangles:
        raise ValueError("No triangles extracted")
    bounds = [[min(v[i] for v in vertices), max(v[i] for v in vertices)] for i in range(3)]
    center = [(lo+hi)/2 for lo, hi in bounds]
    scale = 2.0 / max(hi-lo for lo, hi in bounds)
    text = ["// Locally generated ROM geometry; do not distribute as project source.",
            "static const tgx::fVec3 ARWING_BODY_VERTICES[] = {"]
    for vertex in vertices:
        text.append("    {" + ",".join(f"{(vertex[i]-center[i])*scale:.7f}f" for i in range(3)) + "},")
    text += ["};", "static const uint16_t ARWING_BODY_TRIANGLES[][3] = {"]
    text += ["    {" + ",".join(map(str, tri)) + "}," for tri in triangles]
    text += ["};", ""]
    (local / "arwing_body.inc").write_text("\n".join(text))
    (local / "arwing_body_triangles.bin").write_bytes(b"".join(
        struct.pack("<hhh", *vertices[index]) for tri in triangles for index in tri))
    report = {"source": "aAwBodyDL", "address": "0x03009b60", "segment_sha256": entry["sha256"],
              "vertices": len(vertices), "triangles": len(triangles), "bounds": bounds,
              "opcodes": dict(opcodes), "limitation": "Geometry only; no textures, combiner, skeleton or original game logic"}
    (local / "arwing_body.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    export()
