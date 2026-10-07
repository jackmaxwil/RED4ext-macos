#!/usr/bin/env python3
"""
Build a complete call graph from BL (branch-and-link) instructions and
propagate names from known functions to their callers and callees.

Iterative multi-hop propagation: runs up to MAX_HOPS passes, each time
labelling unnamed callers from named callees and vice versa, using the
previous hop's results as seeds for the next.

Scans __text for all BL instructions, builds caller->callee edges, then
uses function_map.json (105K+ named functions) as initial seed set.
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
MAX_HOPS = 5

WEAK_PREFIXES = (
    "str:", "ref:", "hash_", "calls:", "uses:", "called_by:",
    "cname_ref", "vmethod:", "init:", "hop",
)

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


def find_function_start(text_data: bytes, offset: int) -> int:
    search_start = max(0, offset - 4096)
    pos = (offset // 4) * 4
    while pos >= search_start:
        instr = struct.unpack_from("<I", text_data, pos)[0]
        if (instr & 0xFF0003FF) == 0xD10003FF:
            return pos
        if (instr & 0xFFC003E0) == 0xA98003E0:
            return pos
        pos -= 4
    return offset


def is_strong_name(name: str) -> bool:
    return name and not any(name.startswith(p) for p in WEAK_PREFIXES)


def run():
    binary = str(Path(sys.argv[1]) if len(sys.argv) > 1 else DEFAULT_BINARY)
    out_path = Path(__file__).parent / "callgraph.json"
    funcmap_path = Path(__file__).parent / "function_map.json"

    print(f"Parsing sections from {Path(binary).name}...")
    sections = parse_sections(binary)
    text = sections["__TEXT.__text"]
    text_addr = text["addr"]
    text_offset = text["offset"]
    text_size = text["size"]

    print(f"  __text: 0x{text_addr:X} ({text_size / 1024 / 1024:.1f} MB)")

    print("Scanning for BL instructions...")
    t0 = time.monotonic()

    with open(binary, "rb") as f:
        mm = mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_READ)
        text_data = mm[text_offset: text_offset + text_size]

    caller_map: dict[int, list[int]] = defaultdict(list)
    callee_map: dict[int, list[int]] = defaultdict(list)
    bl_count = 0

    text_lo = text_addr
    text_hi = text_addr + text_size

    for i in range(0, text_size - 4, 4):
        instr = struct.unpack_from("<I", text_data, i)[0]
        if (instr & 0xFC000000) != 0x94000000:
            continue

        bl_count += 1
        pc = text_addr + i
        imm26 = instr & 0x03FFFFFF
        if imm26 & 0x02000000:
            imm26 -= 0x04000000
        target = pc + (imm26 << 2)

        if text_lo <= target < text_hi:
            caller_off = find_function_start(text_data, i)
            caller_addr = text_addr + caller_off
            caller_map[caller_addr].append(target)
            callee_map[target].append(caller_addr)

    scan_time = time.monotonic() - t0
    unique_callers = len(caller_map)
    unique_callees = len(callee_map)

    print(f"  {bl_count:,} BL instructions in {scan_time:.1f}s")
    print(f"  {unique_callers:,} unique caller functions")
    print(f"  {unique_callees:,} unique callee targets")

    # Load function map for name propagation -- use strong names only as seeds
    func_names: dict[int, str] = {}
    if funcmap_path.exists():
        print("Loading function map for propagation...")
        fmap = json.loads(funcmap_path.read_text())
        for addr_str, info in fmap.get("functions", {}).items():
            name = info.get("name")
            if name and is_strong_name(name):
                func_names[int(addr_str, 16)] = name
        print(f"  {len(func_names):,} strong-named seed functions")

    # Multi-hop iterative propagation
    print(f"\nIterative propagation (max {MAX_HOPS} hops)...")
    new_names: dict[int, str] = {}
    hop_stats: list[int] = []

    for hop in range(1, MAX_HOPS + 1):
        hop_new = 0

        # All currently known names (seeds + previous hops)
        all_names: dict[int, str] = {**func_names, **new_names}

        # Forward pass: label unnamed callers from named callees
        for caller_addr, callees in caller_map.items():
            if caller_addr in all_names:
                continue

            named_callees = []
            for callee in callees:
                name = all_names.get(callee)
                if name and is_strong_name(name):
                    named_callees.append(name)

            if len(named_callees) == 1:
                new_names[caller_addr] = f"hop{hop}:calls:{named_callees[0]}"
                hop_new += 1
            elif len(named_callees) >= 2:
                best = min(named_callees, key=len)
                new_names[caller_addr] = f"hop{hop}:uses:{best}"
                hop_new += 1

        # Backward pass: label unnamed callees from named callers
        all_names = {**func_names, **new_names}
        for callee_addr, callers in callee_map.items():
            if callee_addr in all_names:
                continue

            named_callers = []
            for caller in callers:
                name = all_names.get(caller)
                if name and is_strong_name(name):
                    named_callers.append(name)

            if named_callers:
                caller_count = len(callers)
                new_names[callee_addr] = f"hop{hop}:called_by:{named_callers[0]}(+{caller_count-1})"
                hop_new += 1

        hop_stats.append(hop_new)
        print(f"  Hop {hop}: +{hop_new:,} names (total: {len(new_names):,})")

        if hop_new == 0:
            print(f"  Converged at hop {hop}")
            break

    # Compute call frequency stats
    call_freq = defaultdict(int)
    for callers in callee_map.values():
        call_freq[len(callers)] += 1

    hot_functions = sorted(
        [(addr, len(callers)) for addr, callers in callee_map.items()],
        key=lambda x: -x[1]
    )[:500]

    all_names_final = {**func_names, **new_names}

    db = {
        "version": "2.0",
        "generated": time.strftime("%Y-%m-%dT%H:%M:%S"),
        "stats": {
            "total_bl_instructions": bl_count,
            "unique_callers": unique_callers,
            "unique_callees": unique_callees,
            "seed_names": len(func_names),
            "propagated_names": len(new_names),
            "hop_stats": hop_stats,
            "call_frequency_distribution": {
                "1_call": call_freq.get(1, 0),
                "2-5_calls": sum(call_freq.get(i, 0) for i in range(2, 6)),
                "6-20_calls": sum(call_freq.get(i, 0) for i in range(6, 21)),
                "21-100_calls": sum(call_freq.get(i, 0) for i in range(21, 101)),
                "100+_calls": sum(v for k, v in call_freq.items() if k > 100),
            },
        },
        "propagated_names": {
            f"0x{addr:X}": name for addr, name in sorted(new_names.items())
        },
        "hottest_functions": [
            {
                "addr": f"0x{addr:X}",
                "offset": f"0x{addr - IMAGE_BASE:X}",
                "call_count": count,
                "name": all_names_final.get(addr),
            }
            for addr, count in hot_functions
        ],
    }

    out_path.write_text(json.dumps(db, indent=2))
    elapsed = time.monotonic() - t0
    print(f"\nWrote {out_path} ({out_path.stat().st_size / 1024 / 1024:.1f} MB)")
    print(f"Total elapsed: {elapsed:.1f}s")

    print(f"\nHottest functions (most called):")
    for entry in db["hottest_functions"][:15]:
        name = entry.get("name") or "<unnamed>"
        print(f"  {entry['call_count']:5d} calls  {entry['addr']}  {name[:60]}")


if __name__ == "__main__":
    run()
