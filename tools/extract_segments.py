"""Extract SF64 DMA segments locally; this does not generate Starship O2R assets."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import struct

from prepare_rom import SUPPORTED, normalize

ROOT = Path(__file__).resolve().parents[1]


def decode_mio0(data):
    if len(data) < 16 or data[:4] != b"MIO0":
        raise ValueError("Invalid MIO0 header")
    size, pairs, literals = struct.unpack_from(">III", data, 4)
    if size > 32 * 1024 * 1024 or not 16 <= pairs <= literals <= len(data):
        raise ValueError("Invalid MIO0 size or stream offsets")
    masks, bits, mask = 16, 0, 0
    pair_end, mask_end = literals, pairs
    output = bytearray()
    while len(output) < size:
        if bits == 0:
            if masks >= mask_end:
                raise ValueError("Truncated MIO0 mask stream")
            mask, bits = data[masks], 8
            masks += 1
        if mask & 0x80:
            if literals >= len(data):
                raise ValueError("Truncated MIO0 literal stream")
            output.append(data[literals])
            literals += 1
        else:
            if pairs + 2 > pair_end:
                raise ValueError("Truncated MIO0 back-reference stream")
            token = int.from_bytes(data[pairs:pairs+2], "big")
            pairs += 2
            length, distance = (token >> 12) + 3, (token & 0xfff) + 1
            if distance > len(output) or len(output) + length > size:
                raise ValueError("Invalid MIO0 back-reference")
            for _ in range(length):
                output.append(output[-distance])
        mask = (mask << 1) & 0xff
        bits -= 1
    return bytes(output)


def extract(source, destination):
    data, _ = normalize(source.read_bytes())
    sha = hashlib.sha1(data).hexdigest()
    if sha not in SUPPORTED:
        raise ValueError(f"Unsupported ROM: {sha}")
    # Use the pinned upstream's names, DMA offsets and N64 CRC implementation.
    spec = importlib.util.spec_from_file_location("sf64_comptool",
        ROOT / "third_party/starship/tools/comptool.py")
    upstream = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(upstream)
    version = "US 1.1" if data[0x3f] == 1 else "US 1.0"
    table = upstream.file_table_dict[version]
    entries, segments = [], []
    expanded = bytearray()
    for i, name in enumerate(upstream.file_names_us):
        virtual, begin, end, flag = struct.unpack_from(">IIII", data, table + 16*i)
        if not 0 <= begin < end <= len(data) or flag not in (0, 1):
            raise ValueError(f"Invalid DMA entry {i}")
        segment = decode_mio0(data[begin:end]) if flag else data[begin:end]
        if virtual < len(expanded) or virtual + len(segment) > 32 * 1024 * 1024:
            raise ValueError(f"Invalid virtual range for {name}")
        expanded.extend(bytes(virtual - len(expanded)))
        expanded.extend(segment)
        entries.append({"index": i, "name": name, "rom_offset": begin,
                        "rom_size": end-begin, "virtual_offset": virtual,
                        "size": len(segment), "compressed": bool(flag),
                        "sha256": hashlib.sha256(segment).hexdigest()})
        segments.append((name, segment))
    if any(struct.unpack_from(">IIII", data, table + len(entries)*16)):
        raise ValueError("Unexpected DMA entries after named segment list")
    for entry in entries:
        struct.pack_into(">III", expanded, table + entry["index"]*16 + 4,
                         entry["virtual_offset"], entry["virtual_offset"] + entry["size"], 0)
    struct.pack_into(">II", expanded, 0x10, *upstream.calculate_crcs(expanded))
    expected_file = ROOT / f"third_party/starship/yamls/us/rev{data[0x3f]}/header.yaml"
    expected = next(line.split(":", 1)[1].strip() for line in
                    expected_file.read_text().splitlines() if line.startswith("sha1:"))
    expanded_sha = hashlib.sha1(expanded).hexdigest()
    if expanded_sha != expected:
        raise ValueError(f"Expanded ROM SHA-1 mismatch: {expanded_sha} != {expected}")
    destination.mkdir(parents=True, exist_ok=True)
    for name, segment in segments:
        (destination / f"{name}.bin").write_bytes(segment)
    (destination / "baserom.uncompressed.z64").write_bytes(expanded)
    manifest = {"version": version, "rom_sha1": sha,
                "expanded_sha1": expanded_sha, "expanded_size": len(expanded),
                "segment_count": len(entries), "segments": entries,
                "note": "Raw decompressed DMA segments; not decoded/relocated TGX meshes or O2R."}
    (destination / "segments.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(f"Extracted {len(entries)} segments, expanded ROM {len(expanded)} bytes")
    print(f"Expanded SHA-1 matches upstream: {expanded_sha}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path, nargs="?", default=ROOT / "assets/local/baserom.us.z64")
    parser.add_argument("--output", type=Path, default=ROOT / "assets/local/segments")
    args = parser.parse_args()
    try:
        extract(args.source, args.output)
    except (OSError, ValueError, struct.error) as exc:
        parser.exit(1, f"Segment extraction failed: {exc}\n")
