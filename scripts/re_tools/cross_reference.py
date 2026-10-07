#!/usr/bin/env python3
"""
Cross-reference and merge all discovered function databases.

Merges data from:
  1. symbol_database.json    (exported symbols with demangled names)
  2. string_xrefs.json       (functions with string references)
  3. init_functions.json      (static initializer functions)
  4. vtable_database.json     (vtables with method addresses)
  5. rtti_database.json       (if available, from Frida runtime dump)
  6. callgraph.json           (BL instruction call graph propagated names)
  7. nativedb_matches.json    (NativeDB RTTI cross-reference matches)
  8. vtable_propagated.json   (class names propagated across vtable entries)
  9. cname_xrefs.json         (CName hash cross-references, if available)
 10. vtable_hierarchy.json    (vtable inheritance-based class propagation)

Produces a unified function_map.json with every known function address,
its name (if known), category, discovery source, and confidence level.
"""

import argparse
import json
import re
import sys
import time
from collections import defaultdict
from pathlib import Path

IMAGE_BASE = 0x100000000
TOOLS_DIR = Path(__file__).parent


def load_json(path: Path) -> dict | None:
    if path.exists():
        return json.loads(path.read_text())
    return None


def merge_all():
    parser = argparse.ArgumentParser()
    parser.add_argument("--windows-db", type=Path, help="Windows NativeDB JSON export")
    args = parser.parse_args()

    print("Loading databases...")
    t0 = time.monotonic()

    sym_db = load_json(TOOLS_DIR / "symbol_database.json")
    xref_db = load_json(TOOLS_DIR / "string_xrefs.json")
    init_db = load_json(TOOLS_DIR / "init_functions.json")
    vtable_db = load_json(TOOLS_DIR / "vtable_database.json")
    rtti_db = load_json(TOOLS_DIR / "rtti_database.json")

    # Unified function map: address -> info
    func_map: dict[int, dict] = {}

    WEAK_NAME_PREFIXES = ("hash_", "ref:", "str:", "called_by:", "vmethod:", "init:")

    def add_func(addr: int, name: str | None = None, source: str = "unknown",
                 category: str = "unknown", confidence: str = "low",
                 strings: list | None = None, extra: dict | None = None):
        if addr in func_map:
            entry = func_map[addr]
            existing = entry.get("name")
            if name and (
                not existing
                or any(existing.startswith(p) for p in WEAK_NAME_PREFIXES)
            ):
                if not any(name.startswith(p) for p in WEAK_NAME_PREFIXES) or not existing:
                    entry["name"] = name
            entry["sources"].add(source)
            if confidence_rank(confidence) > confidence_rank(entry.get("confidence", "low")):
                entry["confidence"] = confidence
            if category != "unknown" and entry.get("category") == "unknown":
                entry["category"] = category
            if strings:
                entry.setdefault("strings", []).extend(strings)
                entry["strings"] = list(set(entry["strings"]))[:10]
            if extra:
                entry.update(extra)
        else:
            func_map[addr] = {
                "name": name,
                "sources": {source},
                "category": category,
                "confidence": confidence,
                "strings": (strings or [])[:10],
            }
            if extra:
                func_map[addr].update(extra)

    def confidence_rank(c: str) -> int:
        return {"low": 0, "medium": 1, "high": 2, "verified": 3}.get(c, 0)

    # 1. Exported symbols (highest confidence for naming)
    if sym_db:
        count = 0
        for entry in sym_db.get("text_symbols", []):
            addr = int(entry["addr"], 16)
            add_func(addr, name=entry["name"], source="export",
                     category=entry.get("category", "other"),
                     confidence="verified")
            count += 1
        print(f"  Exported symbols: {count}")

    # 2. String xrefs
    if xref_db:
        count = 0
        for addr_str, info in xref_db.get("functions", {}).items():
            addr = int(addr_str, 16)
            strings = info.get("strings", [])

            # Try to derive a name from RTTI strings
            name = None
            cat = "unknown"
            for s in strings:
                if re.match(r"^[a-z]{2,}[A-Z]", s) and len(s) < 60 and " " not in s:
                    name = f"ref:{s}"
                    cat = "rtti_related"
                    break

            if not name and strings:
                # Use first meaningful string as a hint
                for s in strings:
                    if len(s) > 5 and "%" not in s and "/" not in s:
                        name = f"str:{s[:40]}"
                        break

            add_func(addr, name=name, source="string_xref",
                     category=cat, confidence="medium" if name else "low",
                     strings=strings[:5])
            count += 1
        print(f"  String xref functions: {count}")

    # 3. Init functions
    if init_db:
        count = 0
        for entry in init_db.get("init_functions", []):
            addr = int(entry["addr"], 16)
            name = None
            if entry.get("likely_registers"):
                name = f"init:{entry['likely_registers']}"
            add_func(addr, name=name, source="init_offset",
                     category="initialization",
                     confidence="medium" if name else "low",
                     strings=entry.get("strings", [])[:5])

            # Also add BL targets
            for bt in entry.get("bl_targets", []):
                bt_addr = int(bt, 16)
                add_func(bt_addr, source="init_callee",
                         category="initialization", confidence="low")

            count += 1
        print(f"  Init functions: {count}")

    # 4. VTable entries
    if vtable_db:
        vtable_count = 0
        method_count = 0
        for vt in vtable_db.get("vtables", []):
            vtable_count += 1
            class_hint = vt.get("class_hint")
            for idx, addr_str in enumerate(vt.get("entries", [])):
                addr = int(addr_str, 16)
                name = None
                if class_hint:
                    name = f"vmethod:{class_hint}::vtable[{idx}]"
                add_func(addr, name=name, source="vtable",
                         category="virtual_method",
                         confidence="medium" if class_hint else "low",
                         extra={"vtable_index": idx})
                method_count += 1
        print(f"  VTable methods: {method_count} from {vtable_count} vtables")

    # 5. RTTI database (from Frida dump + post-processing)
    if rtti_db:
        count = 0
        for type_name, tdata in rtti_db.get("types", {}).items():
            for fn in tdata.get("functions", []):
                if fn.get("addr"):
                    addr = int(fn["addr"], 16) + IMAGE_BASE
                    name = f"{type_name}::{fn.get('shortName', fn.get('fullName', '?'))}"
                    add_func(addr, name=name, source="rtti_dump",
                             category="rtti_method",
                             confidence="high")
                    count += 1

        for fname, fdata in rtti_db.get("globalFunctions", {}).items():
            if fdata.get("addr"):
                addr = int(fdata["addr"], 16) + IMAGE_BASE
                add_func(addr, name=f"global::{fname}", source="rtti_dump",
                         category="global_function", confidence="high")
                count += 1
        print(f"  RTTI dump functions: {count}")

    # 6. Call graph propagated names
    callgraph_db = load_json(TOOLS_DIR / "callgraph.json")
    if callgraph_db:
        count = 0
        for addr_str, name in callgraph_db.get("propagated_names", {}).items():
            addr = int(addr_str, 16)
            add_func(addr, name=name, source="callgraph",
                     category="propagated", confidence="low")
            count += 1
        print(f"  Call graph propagated: {count}")

    # 7. NativeDB cross-reference (from nativedb_xref.py output)
    nativedb = load_json(TOOLS_DIR / "nativedb_matches.json")
    if nativedb:
        count = 0
        for addr_str, match in nativedb.get("matches", {}).items():
            addr = int(addr_str, 16)
            add_func(addr, name=match["name"], source="nativedb",
                     category=match.get("class", "nativedb_match"),
                     confidence=match.get("confidence", "high"))
            count += 1
        print(f"  NativeDB cross-reference: {count}")

    # 8. VTable class propagation (from vtable_propagator.py output)
    vtprop = load_json(TOOLS_DIR / "vtable_propagated.json")
    if vtprop:
        count = 0
        for addr_str, match in vtprop.get("matches", {}).items():
            addr = int(addr_str, 16)
            add_func(addr, name=match["name"], source="vtable_propagated",
                     category=match.get("class", "vtable_propagated"),
                     confidence=match.get("confidence", "medium"))
            count += 1
        print(f"  VTable propagated: {count}")

    # 9. CName hash cross-references (from cname_hash_scanner.py output)
    cname_db = load_json(TOOLS_DIR / "cname_xrefs.json")
    if cname_db:
        count = 0

        # First: confirmed Class::Method pairs (highest quality from CName)
        for addr_str, info in cname_db.get("func_class_method", {}).items():
            addr = int(addr_str, 16)
            cls = info["class"]
            method = info["method"]
            add_func(addr, name=f"{cls}::{method}", source="cname_xref",
                     category=cls, confidence="high")
            count += 1

        # Then: class-only references
        for addr_str, cnames in cname_db.get("func_cnames", {}).items():
            addr = int(addr_str, 16)
            non_global = [cn for cn in cnames if not cn.startswith("global::")]
            if len(non_global) == 1:
                name = f"{non_global[0]}::cname_ref"
                conf = "medium"
            elif non_global:
                name = f"{non_global[0]}::cname_ref"
                conf = "low"
            else:
                continue
            add_func(addr, name=name, source="cname_xref",
                     category=non_global[0] if non_global else "cname",
                     confidence=conf)
            count += 1
        print(f"  CName hash cross-references: {count}")

    # 10. VTable hierarchy matching (from vtable_hierarchy.py output)
    vthier = load_json(TOOLS_DIR / "vtable_hierarchy.json")
    if vthier:
        count = 0
        for addr_str, match in vthier.get("matches", {}).items():
            addr = int(addr_str, 16)
            add_func(addr, name=match["name"], source="vtable_hierarchy",
                     category=match.get("class", "vtable_hierarchy"),
                     confidence=match.get("confidence", "low"))
            count += 1
        print(f"  VTable hierarchy: {count}")

    # Convert sets to lists for JSON serialization
    for entry in func_map.values():
        entry["sources"] = sorted(entry["sources"])

    # Compute stats
    total = len(func_map)
    named = sum(1 for v in func_map.values() if v.get("name"))
    by_source = defaultdict(int)
    by_category = defaultdict(int)
    by_confidence = defaultdict(int)
    multi_source = 0

    for v in func_map.values():
        for s in v["sources"]:
            by_source[s] += 1
        by_category[v.get("category", "unknown")] += 1
        by_confidence[v.get("confidence", "low")] += 1
        if len(v["sources"]) > 1:
            multi_source += 1

    output = {
        "version": "1.0",
        "generated": time.strftime("%Y-%m-%dT%H:%M:%S"),
        "stats": {
            "total_functions": total,
            "named_functions": named,
            "multi_source_functions": multi_source,
            "by_source": dict(sorted(by_source.items(), key=lambda kv: -kv[1])),
            "by_category": dict(sorted(by_category.items(), key=lambda kv: -kv[1])),
            "by_confidence": dict(sorted(by_confidence.items(), key=lambda kv: -kv[1])),
        },
        "functions": {
            f"0x{addr:X}": {
                "offset": f"0x{addr - IMAGE_BASE:X}",
                "name": v.get("name"),
                "sources": v["sources"],
                "category": v.get("category", "unknown"),
                "confidence": v.get("confidence", "low"),
            }
            for addr, v in sorted(func_map.items())
        },
    }

    out_path = TOOLS_DIR / "function_map.json"
    out_path.write_text(json.dumps(output, indent=2))
    elapsed = time.monotonic() - t0

    print(f"\n{'='*60}")
    print(f"Wrote {out_path} ({out_path.stat().st_size / 1024 / 1024:.1f} MB)")
    print(f"Total elapsed: {elapsed:.1f}s")
    print(f"\n  Total unique function addresses: {total:,}")
    print(f"  Named functions: {named:,} ({named/total*100:.1f}%)")
    print(f"  Multi-source corroborated: {multi_source:,}")
    print(f"\n  By source:")
    for s, c in sorted(by_source.items(), key=lambda kv: -kv[1]):
        print(f"    {s:20s} {c:6,}")
    print(f"\n  By confidence:")
    for c, n in sorted(by_confidence.items(), key=lambda kv: -kv[1]):
        print(f"    {c:12s} {n:6,}")

    # Coverage estimate
    est_total = 195_000
    print(f"\n  Estimated binary coverage: {total/est_total*100:.1f}% of ~{est_total:,} functions")


if __name__ == "__main__":
    merge_all()
