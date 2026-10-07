#!/usr/bin/env python3
"""
Phase A.1: Export all named symbols from the Cyberpunk 2077 binary into a
structured JSON database with demangled names, addresses, and categories.

Reads exported Mach-O symbols via `nm`, demangles them via `c++filt`, and
classifies each into a subsystem category.
"""

import json
import re
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

CATEGORY_RULES = [
    (re.compile(r"red::memory|StaticPoolStorage|Pool[A-Z]"), "memory"),
    (re.compile(r"red::VTable"), "vtable_ctor"),
    (re.compile(r"funcOperator|OpTest|OpAssign|OpAdd|OpSub|OpMul|OpDiv"), "script_ops"),
    (re.compile(r"InGameConfig"), "config"),
    (re.compile(r"CScriptStackFrame|IScriptable|CStack"), "scripting"),
    (re.compile(r"red::DynArray|red::HashMap|red::SharedPtr|red::UniquePtr"), "containers"),
    (re.compile(r"AI::|ai[A-Z]"), "ai"),
    (re.compile(r"vehicle::|Vehicle"), "vehicle"),
    (re.compile(r"game::|Game"), "game"),
    (re.compile(r"ink::|Ink"), "ink_ui"),
    (re.compile(r"world::|World"), "world"),
    (re.compile(r"ent::|Entity"), "entity"),
    (re.compile(r"physics::|Physics"), "physics"),
    (re.compile(r"anim::|Anim"), "animation"),
    (re.compile(r"audio::|Audio"), "audio"),
    (re.compile(r"work::|Workspot"), "workspots"),
    (re.compile(r"quest::|Quest"), "quest"),
    (re.compile(r"save::|Save"), "save"),
    (re.compile(r"net::|Multiplayer"), "network"),
    (re.compile(r"nv::cloth|physx::"), "middleware"),
    (re.compile(r"rtti::|RTTI|CClass|CBaseRTTI"), "rtti"),
    (re.compile(r"GpuApi|Gpu"), "gpu"),
    (re.compile(r"red::"), "red_core"),
]


def categorize(demangled: str) -> str:
    for pattern, category in CATEGORY_RULES:
        if pattern.search(demangled):
            return category
    return "other"


def run():
    binary = Path(sys.argv[1]) if len(sys.argv) > 1 else DEFAULT_BINARY
    out_path = Path(sys.argv[2]) if len(sys.argv) > 2 else Path(__file__).parent / "symbol_database.json"

    if not binary.exists():
        sys.exit(f"Binary not found: {binary}")

    print(f"Reading symbols from {binary.name}...")
    t0 = time.monotonic()

    nm = subprocess.run(
        ["nm", "-gU", str(binary)],
        capture_output=True, text=True, check=True,
    )

    text_symbols = []
    data_symbols = []
    mangled_names = []

    for line in nm.stdout.splitlines():
        parts = line.split(None, 2)
        if len(parts) < 3:
            continue
        addr_str, sym_type, mangled = parts
        addr = int(addr_str, 16)
        offset = addr - IMAGE_BASE
        mangled_names.append(mangled)

        entry = {
            "address": addr,
            "offset": offset,
            "mangled": mangled,
            "type": sym_type,
        }
        if sym_type == "T":
            text_symbols.append(entry)
        elif sym_type in ("D", "S"):
            data_symbols.append(entry)

    print(f"  {len(text_symbols)} text, {len(data_symbols)} data symbols in {time.monotonic()-t0:.1f}s")

    print("Demangling...")
    t1 = time.monotonic()
    demangle = subprocess.run(
        ["c++filt"],
        input="\n".join(mangled_names),
        capture_output=True, text=True, check=True,
    )
    demangled_list = demangle.stdout.splitlines()
    print(f"  Done in {time.monotonic()-t1:.1f}s")

    demangled_map = dict(zip(mangled_names, demangled_list))

    categories = defaultdict(int)

    for entry in text_symbols + data_symbols:
        dm = demangled_map.get(entry["mangled"], entry["mangled"])
        entry["demangled"] = dm
        cat = categorize(dm)
        entry["category"] = cat
        categories[cat] += 1

    text_symbols.sort(key=lambda e: e["address"])
    data_symbols.sort(key=lambda e: e["address"])

    db = {
        "version": "2.0",
        "binary": str(binary.name),
        "generated": time.strftime("%Y-%m-%dT%H:%M:%S"),
        "stats": {
            "text_symbols": len(text_symbols),
            "data_symbols": len(data_symbols),
            "total": len(text_symbols) + len(data_symbols),
            "categories": dict(sorted(categories.items(), key=lambda kv: -kv[1])),
        },
        "text_symbols": [
            {
                "addr": f"0x{e['address']:X}",
                "offset": f"0x{e['offset']:X}",
                "name": e["demangled"],
                "category": e["category"],
            }
            for e in text_symbols
        ],
        "data_symbols": [
            {
                "addr": f"0x{e['address']:X}",
                "offset": f"0x{e['offset']:X}",
                "name": e["demangled"],
                "category": e["category"],
            }
            for e in data_symbols
        ],
    }

    out_path.write_text(json.dumps(db, indent=2))
    elapsed = time.monotonic() - t0
    print(f"\nWrote {out_path} ({out_path.stat().st_size / 1024:.0f} KB)")
    print(f"Total: {db['stats']['total']} symbols in {elapsed:.1f}s")
    print("\nCategory breakdown:")
    for cat, count in sorted(categories.items(), key=lambda kv: -kv[1]):
        print(f"  {cat:20s} {count:6d}")


if __name__ == "__main__":
    run()
