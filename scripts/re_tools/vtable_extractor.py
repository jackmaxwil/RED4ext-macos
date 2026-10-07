#!/usr/bin/env python3
"""
Phase C.1: Extract vtables from __DATA_CONST and correlate with class names.

Scans __DATA_CONST.__const for consecutive pointer-sized values that all
reference __text (function pointers), identifies them as vtables, and
cross-references with the string_xrefs database to name the classes.

Uses chained fixup decoding: raw_ptr = (raw & 0xFFFFFFFF) + IMAGE_BASE
"""

import json
import mmap
import struct
import subprocess
import sys
import time
from collections import defaultdict
from pathlib import Path

IMAGE_BASE = 0x100000000

DEFAULT_BINARY = (
    Path.home()
    / "Library/Application Support/Steam/steamapps/common"
    / "Cyberpunk 2077/Cyberpunk2077.app/Contents/MacOS/Cyberpunk2077"
)

MIN_VTABLE_ENTRIES = 2
MAX_VTABLE_ENTRIES = 500


def parse_sections(binary: str) -> dict:
    result = subprocess.run(["otool", "-l", binary], capture_output=True, text=True)
    lines = result.stdout.split("\n")
    sections = {}
    current_seg = current_sect = None
    info = {}
    for line in lines:
        s = line.strip()
        if s.startswith("segname "):
            current_seg = s.split()[-1]
        elif s.startswith("sectname "):
            if current_sect and info:
                sections[f"{info.get('seg','?')}.{current_sect}"] = info
            current_sect = s.split()[-1]
            info = {"seg": current_seg}
        elif s.startswith("addr "):
            info["addr"] = int(s.split()[-1], 16)
        elif s.startswith("size ") and "align" not in s:
            info["size"] = int(s.split()[-1], 16)
        elif s.startswith("offset ") and "reloff" not in s:
            info["offset"] = int(s.split()[-1])
    if current_sect and info:
        sections[f"{info.get('seg','?')}.{current_sect}"] = info
    return sections


def decode_chained_ptr(raw: int) -> int | None:
    """Decode a chained fixup pointer to an absolute address."""
    # Chained fixups: the low 32 bits are the offset from image base
    # Bit 63 set means it's a rebase, not a bind
    if raw == 0:
        return None
    # Check if high bit indicates a chained fixup rebase
    if raw & (1 << 63):
        offset = raw & 0xFFFFFFFF
        return IMAGE_BASE + offset
    # Regular pointer (already absolute in memory, but relative in file)
    offset = raw & 0xFFFFFFFF
    addr = IMAGE_BASE + offset
    return addr


def run():
    binary = str(Path(sys.argv[1]) if len(sys.argv) > 1 else DEFAULT_BINARY)
    out_path = Path(__file__).parent / "vtable_database.json"
    xref_path = Path(__file__).parent / "string_xrefs.json"
    sym_path = Path(__file__).parent / "symbol_database.json"

    print(f"Parsing sections from {Path(binary).name}...")
    sections = parse_sections(binary)

    dc_const = sections.get("__DATA_CONST.__const")
    text_sect = sections["__TEXT.__text"]
    if not dc_const:
        sys.exit("__DATA_CONST.__const not found")

    text_lo = text_sect["addr"]
    text_hi = text_sect["addr"] + text_sect["size"]

    dc_addr = dc_const["addr"]
    dc_offset = dc_const["offset"]
    dc_size = dc_const["size"]

    print(f"  __DATA_CONST.__const: 0x{dc_addr:X}, {dc_size / 1024 / 1024:.1f} MB")
    print(f"  __text: 0x{text_lo:X}-0x{text_hi:X}")

    # Load string xrefs for cross-referencing
    xref_db = None
    if xref_path.exists():
        xref_db = json.loads(xref_path.read_text())
        print(f"  Loaded {xref_db['stats']['unique_functions']} function xrefs")

    # Load symbol database for exported function matching
    sym_db = {}
    if sym_path.exists():
        sdb = json.loads(sym_path.read_text())
        for entry in sdb.get("text_symbols", []):
            sym_db[int(entry["addr"], 16)] = entry["name"]
        print(f"  Loaded {len(sym_db)} exported symbols")

    print(f"Scanning __DATA_CONST.__const for vtables...")
    t0 = time.monotonic()

    with open(binary, "rb") as f:
        mm = mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_READ)
        dc_data = mm[dc_offset: dc_offset + dc_size]

    vtables = []
    i = 0
    ptr_size = 8
    entry_count = dc_size // ptr_size

    while i < entry_count:
        raw = struct.unpack_from("<Q", dc_data, i * ptr_size)[0]
        decoded = decode_chained_ptr(raw)

        if decoded is not None and text_lo <= decoded < text_hi:
            # Start of potential vtable
            vtable_start = i
            vtable_addrs = [decoded]

            j = i + 1
            while j < entry_count:
                raw2 = struct.unpack_from("<Q", dc_data, j * ptr_size)[0]
                decoded2 = decode_chained_ptr(raw2)
                if decoded2 is not None and text_lo <= decoded2 < text_hi:
                    vtable_addrs.append(decoded2)
                    j += 1
                else:
                    break

            if len(vtable_addrs) >= MIN_VTABLE_ENTRIES:
                vtable_va = dc_addr + vtable_start * ptr_size
                vtables.append({
                    "addr": vtable_va,
                    "offset": vtable_va - IMAGE_BASE,
                    "count": len(vtable_addrs),
                    "entries": vtable_addrs,
                })
            i = j
        else:
            i += 1

    scan_time = time.monotonic() - t0
    print(f"  Found {len(vtables)} vtables ({sum(v['count'] for v in vtables)} total entries) in {scan_time:.1f}s")

    # Cross-reference vtable entries with known symbols and string xrefs
    print("Cross-referencing vtable entries...")

    named_vtables = 0
    total_method_addrs = set()

    for vt in vtables:
        # Try to name vtable entries from exported symbols
        vt_named_entries = []
        for func_addr in vt["entries"]:
            total_method_addrs.add(func_addr)
            name = sym_db.get(func_addr)
            if name:
                vt_named_entries.append(name)

        if vt_named_entries:
            # Use the most common class prefix to name the vtable
            vt["named_entries_sample"] = vt_named_entries[:5]

        # Try to find which class owns this vtable by checking nearby
        # string references (RTTI registration code stores vtable ptr
        # near class name string references)
        if xref_db:
            # Check if any vtable entry function references an RTTI name string
            for func_addr in vt["entries"][:3]:
                key = f"0x{func_addr:X}"
                func_info = xref_db.get("functions", {}).get(key)
                if func_info:
                    for s in func_info.get("strings", []):
                        if len(s) > 3 and s[0].islower() and any(c.isupper() for c in s[1:]):
                            if "class_hint" not in vt:
                                vt["class_hint"] = s
                                named_vtables += 1
                            break
                    break

    print(f"  Named {named_vtables}/{len(vtables)} vtables via string xrefs")
    print(f"  {len(total_method_addrs)} unique virtual method addresses")

    # Build output
    db = {
        "version": "1.0",
        "binary": Path(binary).name,
        "generated": time.strftime("%Y-%m-%dT%H:%M:%S"),
        "stats": {
            "vtable_count": len(vtables),
            "total_entries": sum(v["count"] for v in vtables),
            "unique_method_addrs": len(total_method_addrs),
            "named_vtables": named_vtables,
            "size_distribution": {
                "2-5": sum(1 for v in vtables if 2 <= v["count"] <= 5),
                "6-10": sum(1 for v in vtables if 6 <= v["count"] <= 10),
                "11-20": sum(1 for v in vtables if 11 <= v["count"] <= 20),
                "21-50": sum(1 for v in vtables if 21 <= v["count"] <= 50),
                "51+": sum(1 for v in vtables if v["count"] > 50),
            },
        },
        "vtables": [
            {
                "addr": f"0x{v['addr']:X}",
                "offset": f"0x{v['offset']:X}",
                "entry_count": v["count"],
                "class_hint": v.get("class_hint"),
                "named_sample": v.get("named_entries_sample", [])[:3],
                "entries": [f"0x{a:X}" for a in v["entries"]],
            }
            for v in sorted(vtables, key=lambda v: -v["count"])
        ],
    }

    out_path.write_text(json.dumps(db, indent=2))
    elapsed = time.monotonic() - t0
    print(f"\nWrote {out_path} ({out_path.stat().st_size / 1024 / 1024:.1f} MB)")
    print(f"VTable size distribution:")
    for k, v in db["stats"]["size_distribution"].items():
        print(f"  {k} entries: {v} vtables")


if __name__ == "__main__":
    run()
