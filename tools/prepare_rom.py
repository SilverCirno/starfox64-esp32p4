"""Validate a local SF64 dump and normalize it to N64 big-endian byte order."""
import argparse
import hashlib
import json
from pathlib import Path
import zipfile

SUPPORTED = {
    "d8b1088520f7c5f81433292a9258c1184afa1457": "US 1.0",
    "09f0d105f476b00efa5303a3ebc42e60a7753b7a": "US 1.1 Rev A",
}


def normalize(data):
    magic = data[:4]
    if len(data) != 12 * 1024 * 1024:
        raise ValueError(f"Expected a 12 MiB SF64 dump, got {len(data)} bytes")
    if magic == bytes.fromhex("80371240"):
        return data, "big-endian (.z64)"
    result = bytearray(len(data))
    if magic == bytes.fromhex("37804012"):
        result[0::2], result[1::2] = data[1::2], data[0::2]
        return bytes(result), "byte-swapped (.v64)"
    if magic == bytes.fromhex("40123780"):
        for i in range(4):
            result[i::4] = data[3-i::4]
        return bytes(result), "little-endian (.n64)"
    raise ValueError(f"Unknown N64 magic {magic.hex()}")


def prepare(source, output):
    if zipfile.is_zipfile(source):
        with zipfile.ZipFile(source) as archive:
            entries = [e for e in archive.infolist()
                       if Path(e.filename).suffix.lower() in (".z64", ".v64", ".n64")]
            if len(entries) != 1:
                raise ValueError("ZIP must contain exactly one N64 ROM")
            entry = entries[0]
            if entry.file_size != 12 * 1024 * 1024:
                raise ValueError("ROM ZIP entry is not 12 MiB")
            data = archive.read(entry)
            member = entry.filename
    else:
        if source.stat().st_size != 12 * 1024 * 1024:
            raise ValueError("ROM file is not 12 MiB")
        data, member = source.read_bytes(), source.name
    canonical, order = normalize(data)
    sha1 = hashlib.sha1(canonical).hexdigest()
    if sha1 not in SUPPORTED:
        raise ValueError(f"Unsupported ROM SHA-1 after normalization: {sha1}")
    manifest = {
        "source_member": member, "source_byte_order": order,
        "source_sha1": hashlib.sha1(data).hexdigest(),
        "version": SUPPORTED[sha1], "size": len(canonical),
        "sha1_z64": sha1, "sha256_z64": hashlib.sha256(canonical).hexdigest(),
        "title": canonical[0x20:0x34].decode("ascii").strip(),
        "cartridge_id": canonical[0x3b:0x3f].decode("ascii"),
        "revision": canonical[0x3f],
        "note": "Validated ROM only; game assets have not been extracted.",
    }
    output.mkdir(parents=True, exist_ok=True)
    target = output / "baserom.us.z64"
    if target.exists() and target.read_bytes() != canonical:
        raise ValueError(f"Refusing to replace a different ROM: {target}")
    target.write_bytes(canonical)
    (output / "rom_manifest.json").write_text(
        json.dumps(manifest, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    return manifest


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("--output", type=Path, default=Path("assets/local"))
    args = parser.parse_args()
    try:
        print(json.dumps(prepare(args.source, args.output), indent=2, ensure_ascii=False))
    except (ValueError, OSError, zipfile.BadZipFile) as exc:
        parser.exit(1, f"ROM preparation failed: {exc}\n")
