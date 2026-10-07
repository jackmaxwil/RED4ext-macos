#!/usr/bin/env python3
"""
Phase A.3: Parse __init_offsets section to catalog all static
initializer/registration functions.

These are C++ static constructors that run at image load time (dlopen).
They typically register RTTI types, set up singletons, and initialize
global state. Cross-references with string_xrefs.json to name each
init function by the strings it references.
"""

import json
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


def run():
    binary = str(Path(sys.argv[1]) if len(sys.argv) > 1 else DEFAULT_BINARY)
    out_path = Path(__file__).parent / "init_functions.json"
    xref_path = Path(__file__).parent / "string_xrefs.json"

    print(f"Parsing sections from {Path(binary).name}...")
    sections = parse_sections(binary)

    init_sect = sections.get("__TEXT.__init_offsets")
    if not init_sect:
        sys.exit("__init_offsets section not found")

    text_sect = sections["__TEXT.__text"]

    print(f"  __init_offsets: offset=0x{init_sect['offset']:X}, size=0x{init_sect['size']:X}")
    count = init_sect["size"] // 4
    print(f"  Expected entries: {count}")

    sect_addr = init_sect["addr"]

    with open(binary, "rb") as f:
        f.seek(init_sect["offset"])
        data = f.read(init_sect["size"])

    init_addrs = []
    for i in range(count):
        raw = struct.unpack_from("<I", data, i * 4)[0]
        func_addr = IMAGE_BASE + raw
        init_addrs.append(func_addr)

    unique_addrs = sorted(set(init_addrs))
    print(f"  Unique init function addresses: {len(unique_addrs)}")

    text_lo = text_sect["addr"]
    text_hi = text_sect["addr"] + text_sect["size"]
    in_text = [a for a in unique_addrs if text_lo <= a < text_hi]
    print(f"  In __text segment: {len(in_text)}")

    xref_db = None
    if xref_path.exists():
        print(f"Loading string xrefs from {xref_path.name}...")
        xref_db = json.loads(xref_path.read_text())
        print(f"  {xref_db['stats']['unique_functions']} functions with string refs")

    # Load full binary for BL target tracing
    print("Tracing BL targets from init stubs...")
    seg_info = sections.get("__TEXT", {})
    text_seg_addr = 0x100000000
    text_seg_size = 0
    for key, val in sections.items():
        if key.startswith("__TEXT."):
            end = val["addr"] + val["size"]
            if end > text_seg_addr + text_seg_size:
                text_seg_size = end - text_seg_addr

    with open(binary, "rb") as f:
        full_data = f.read()

    def addr_to_file_offset(addr):
        return addr - IMAGE_BASE

    def read_bl_targets(func_addr, max_instrs=32):
        """Read up to max_instrs instructions, return BL targets."""
        targets = []
        foff = addr_to_file_offset(func_addr)
        if foff < 0 or foff + max_instrs * 4 > len(full_data):
            return targets
        for i in range(max_instrs):
            pos = foff + i * 4
            if pos + 4 > len(full_data):
                break
            instr = struct.unpack_from("<I", full_data, pos)[0]
            pc = func_addr + i * 4
            # BL imm26 => 0x94000000 | imm26
            if (instr & 0xFC000000) == 0x94000000:
                imm26 = instr & 0x03FFFFFF
                if imm26 & 0x02000000:
                    imm26 -= 0x04000000
                target = pc + (imm26 << 2)
                targets.append(target)
            # RET => stop scanning
            if instr == 0xD65F03C0:
                break
        return targets

    entries = []
    named = 0
    categories = defaultdict(int)

    for addr in unique_addrs:
        offset = addr - IMAGE_BASE
        entry = {
            "addr": f"0x{addr:X}",
            "offset": f"0x{offset:X}",
            "in_text": text_lo <= addr < text_hi,
        }

        # Trace BL targets from this init stub
        bl_targets = read_bl_targets(addr)
        if bl_targets:
            entry["bl_targets"] = [f"0x{t:X}" for t in bl_targets]

        # Check if the init function itself or its BL targets have string xrefs
        check_addrs = [addr] + bl_targets
        matched = False

        if xref_db:
            for check_addr in check_addrs:
                key = f"0x{check_addr:X}"
                func_info = xref_db.get("functions", {}).get(key)
                if func_info:
                    strings = func_info.get("strings", [])
                    entry["strings"] = strings[:10]

                    rtti_names = [
                        s for s in strings
                        if len(s) > 3 and s[0].islower()
                        and any(c.isupper() for c in s[1:])
                        and " " not in s and "/" not in s and "%" not in s
                    ]
                    if rtti_names:
                        entry["likely_registers"] = rtti_names[0]
                        named += 1
                        categories["rtti_registration"] += 1
                    elif any("Pool" in s for s in strings):
                        entry["likely_type"] = "pool_init"
                        categories["pool_init"] += 1
                    elif strings:
                        entry["likely_type"] = "other_init"
                        categories["other_init"] += 1
                    else:
                        categories["no_strings"] += 1
                    matched = True
                    break

            if not matched:
                categories["no_xref_match"] += 1
        else:
            categories["no_xref_data"] += 1

        entries.append(entry)

    db = {
        "version": "1.0",
        "binary": Path(binary).name,
        "generated": time.strftime("%Y-%m-%dT%H:%M:%S"),
        "stats": {
            "total_init_entries": count,
            "unique_addresses": len(unique_addrs),
            "in_text_segment": len(in_text),
            "named_via_strings": named,
            "categories": dict(categories),
        },
        "init_functions": entries,
    }

    out_path.write_text(json.dumps(db, indent=2))
    print(f"\nWrote {out_path} ({out_path.stat().st_size / 1024:.0f} KB)")
    print(f"  {named}/{len(unique_addrs)} init functions named via string xrefs")
    print(f"  Categories: {dict(categories)}")


if __name__ == "__main__":
    run()
