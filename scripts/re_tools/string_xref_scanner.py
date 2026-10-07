#!/usr/bin/env python3
"""
Phase A.2: Exhaustive ADRP+ADD string cross-reference scanner.

Scans the entire __text section for ARM64 ADRP+ADD instruction pairs that
reference addresses in __cstring (or __const), mapping every string reference
back to its containing function.

Outputs a JSON database of:
  - string -> list of referencing function addresses
  - function -> list of referenced strings
"""

import json
import mmap
import re
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


def decode_adrp(instr: int, pc: int) -> int | None:
    if (instr & 0x9F000000) != 0x90000000:
        return None
    rd = instr & 0x1F
    immhi = (instr >> 5) & 0x7FFFF
    immlo = (instr >> 29) & 0x3
    imm = (immhi << 2) | immlo
    if imm & 0x100000:
        imm -= 0x200000
    page = (pc & ~0xFFF) + (imm << 12)
    return (rd, page)


def decode_add_imm(instr: int) -> tuple | None:
    if (instr & 0xFF800000) not in (0x91000000, 0x91400000):
        return None
    rd = instr & 0x1F
    rn = (instr >> 5) & 0x1F
    imm12 = (instr >> 10) & 0xFFF
    shift = 12 if (instr & 0x00400000) else 0
    return (rd, rn, imm12 << shift)


def find_function_start(text_data: bytes, offset_in_text: int) -> int:
    """Walk backwards from offset to find the nearest SUB SP,SP,#imm or STP ...,[SP,#-imm]! prologue."""
    search_start = max(0, offset_in_text - 4096)
    pos = (offset_in_text // 4) * 4

    while pos >= search_start:
        instr = struct.unpack_from("<I", text_data, pos)[0]
        # SUB SP, SP, #imm => 0xD10...FF
        if (instr & 0xFF0003FF) == 0xD10003FF:
            return pos
        # STP Xn, Xm, [SP, #-imm]! (pre-index, 64-bit) with Rn=SP
        if (instr & 0xFFC003E0) == 0xA98003E0:
            return pos
        pos -= 4

    return offset_in_text


def run():
    binary = str(Path(sys.argv[1]) if len(sys.argv) > 1 else DEFAULT_BINARY)
    out_path = Path(sys.argv[2]) if len(sys.argv) > 2 else Path(__file__).parent / "string_xrefs.json"

    print(f"Parsing sections from {Path(binary).name}...")
    sections = parse_sections(binary)

    text = sections["__TEXT.__text"]
    cstring = sections["__TEXT.__cstring"]
    text_const = sections.get("__TEXT.__const", {"addr": 0, "size": 0})

    string_range_lo = cstring["addr"]
    string_range_hi = cstring["addr"] + cstring["size"]
    const_range_lo = text_const["addr"]
    const_range_hi = text_const["addr"] + text_const["size"]

    text_addr = text["addr"]
    text_offset = text["offset"]
    text_size = text["size"]

    print(f"  __text: 0x{text_addr:X} ({text_size / 1024 / 1024:.1f} MB)")
    print(f"  __cstring: 0x{string_range_lo:X}-0x{string_range_hi:X} ({cstring['size'] / 1024:.0f} KB)")

    print("Loading strings from __cstring...")
    t0 = time.monotonic()

    with open(binary, "rb") as f:
        f.seek(cstring["offset"])
        cstring_data = f.read(cstring["size"])

    string_map: dict[int, str] = {}
    pos = 0
    while pos < len(cstring_data):
        end = cstring_data.find(b"\x00", pos)
        if end == -1:
            break
        if end > pos:
            try:
                s = cstring_data[pos:end].decode("utf-8", errors="replace")
                if len(s) >= 2:
                    string_map[cstring["addr"] + pos] = s
            except Exception:
                pass
        pos = end + 1

    print(f"  {len(string_map)} strings in {time.monotonic()-t0:.1f}s")

    print(f"Scanning __text for ADRP+ADD pairs ({text_size / 1024 / 1024:.1f} MB)...")
    t1 = time.monotonic()

    with open(binary, "rb") as f:
        mm = mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_READ)

        text_data = mm[text_offset : text_offset + text_size]

        adrp_regs: dict[int, tuple[int, int]] = {}
        xrefs: list[tuple[int, int, str]] = []

        for i in range(0, text_size - 4, 4):
            instr = struct.unpack_from("<I", text_data, i)[0]
            pc = text_addr + i

            adrp = decode_adrp(instr, pc)
            if adrp is not None:
                rd, page = adrp
                adrp_regs[rd] = (page, i)
                continue

            add = decode_add_imm(instr)
            if add is not None:
                rd, rn, imm = add
                if rn in adrp_regs:
                    page, adrp_off = adrp_regs[rn]
                    if (i - adrp_off) <= 64:
                        target = page + imm
                        if string_range_lo <= target < string_range_hi:
                            s = string_map.get(target)
                            if s:
                                xrefs.append((i, target, s))

        mm.close()

    scan_time = time.monotonic() - t1
    print(f"  Found {len(xrefs)} string references in {scan_time:.1f}s")

    print("Mapping references to functions...")
    t2 = time.monotonic()

    func_strings: dict[int, list[str]] = defaultdict(list)
    string_funcs: dict[str, list[int]] = defaultdict(list)

    for instr_off, target_addr, string_val in xrefs:
        func_off = find_function_start(text_data, instr_off)
        func_addr = text_addr + func_off
        func_strings[func_addr].append(string_val)
        string_funcs[string_val].append(func_addr)

    for key in string_funcs:
        string_funcs[key] = sorted(set(string_funcs[key]))

    unique_funcs = len(func_strings)
    unique_strings_hit = len(string_funcs)
    print(f"  {unique_funcs} unique functions reference {unique_strings_hit} unique strings")
    print(f"  Mapping done in {time.monotonic()-t2:.1f}s")

    rtti_re = re.compile(r"^[a-z]{2,}[A-Z]")
    cats = defaultdict(int)
    for s in string_funcs:
        if rtti_re.match(s) and len(s) < 80:
            cats["rtti_name"] += 1
        elif "error" in s.lower() or "fail" in s.lower() or "assert" in s.lower():
            cats["error_assert"] += 1
        elif "%" in s:
            cats["format_string"] += 1
        elif "/" in s and "." in s:
            cats["source_path"] += 1
        else:
            cats["other"] += 1

    db = {
        "version": "1.0",
        "binary": Path(binary).name,
        "generated": time.strftime("%Y-%m-%dT%H:%M:%S"),
        "stats": {
            "total_xrefs": len(xrefs),
            "unique_functions": unique_funcs,
            "unique_strings_referenced": unique_strings_hit,
            "string_categories": dict(cats),
            "total_strings_in_binary": len(string_map),
        },
        "functions": {
            f"0x{addr:X}": {
                "offset": f"0x{addr - IMAGE_BASE:X}",
                "strings": strs[:20],
                "string_count": len(strs),
            }
            for addr, strs in sorted(func_strings.items())
        },
        "string_index": {
            s: [f"0x{a:X}" for a in addrs]
            for s, addrs in sorted(string_funcs.items())
            if len(addrs) <= 50
        },
    }

    out_path.write_text(json.dumps(db, indent=2))
    elapsed = time.monotonic() - t0
    print(f"\nWrote {out_path} ({out_path.stat().st_size / 1024 / 1024:.1f} MB)")
    print(f"Total elapsed: {elapsed:.1f}s")


if __name__ == "__main__":
    run()
