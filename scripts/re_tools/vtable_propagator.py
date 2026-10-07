#!/usr/bin/env python3
"""
VTable Class Propagation: For each vtable that has at least one entry with a
qualified class name (ClassName::Method), propagate that class identity to all
other entries in the vtable.

Uses NativeDB function lists to assign actual method names where the vtable
index maps to a known function.  Static functions (flag bit 4) are excluded
from vtable indexing since they don't occupy vtable slots.

Reads:  function_map.json, vtable_database.json, nativedb_data/classes.json,
        cname_xrefs.json (optional, for additional vtable identification)
Writes: vtable_propagated.json
"""

import json
import time
from collections import Counter, defaultdict
from pathlib import Path

TOOLS_DIR = Path(__file__).parent
NATIVEDB_DIR = TOOLS_DIR / "nativedb_data"

WEAK_PREFIXES = ("ref:", "str:", "called_by:", "vmethod:", "init:", "hash_")


def load_json(path: Path) -> dict | list | None:
    if path.exists():
        return json.loads(path.read_text())
    print(f"  WARNING: {path} not found")
    return None


def build_nativedb_vtable_map(classes: list[dict]) -> dict[str, list[str]]:
    """Build class_name -> ordered list of non-static function names.

    NativeDB schema: b = class name, f = functions list.
    Function flags: bit 4 (16) = static.  Static functions are excluded
    because they don't occupy vtable slots.
    """
    result: dict[str, list[str]] = {}
    for c in classes:
        name = c.get("b", "")
        if not name:
            continue
        funcs = c.get("f", [])
        non_static = []
        for fn in funcs:
            flags = fn.get("d", 0)
            if flags & 16:
                continue
            short = fn.get("b") or fn.get("a", "").split(";")[0]
            non_static.append(short or "?")
        result[name] = non_static
    return result


def main():
    print("VTable Class Propagation")
    print("=" * 60)
    t0 = time.monotonic()

    print("\nLoading databases...")
    fm_db = load_json(TOOLS_DIR / "function_map.json")
    vt_db = load_json(TOOLS_DIR / "vtable_database.json")
    ndb_classes = load_json(NATIVEDB_DIR / "classes.json")

    if not fm_db or not vt_db:
        print("ERROR: function_map.json and vtable_database.json are required")
        return

    funcs = fm_db.get("functions", {})
    vtables = vt_db.get("vtables", [])
    print(f"  Functions: {len(funcs):,}")
    print(f"  VTables: {len(vtables):,}")

    ndb_vtable_map: dict[str, list[str]] = {}
    if ndb_classes:
        ndb_vtable_map = build_nativedb_vtable_map(ndb_classes)
        print(f"  NativeDB classes with vtable maps: {len(ndb_vtable_map):,}")

    # Build set of valid NativeDB class names for vote filtering
    ndb_class_set: set[str] = set(ndb_vtable_map.keys()) if ndb_vtable_map else set()
    print(f"  Valid NativeDB class names for voting: {len(ndb_class_set):,}")

    # Build entry-to-vtable-count index: how many vtables contain each function?
    # Functions in many vtables are inherited base class methods -- low signal.
    print("\nBuilding entry frequency index...")
    entry_vtable_count: Counter[str] = Counter()
    for vt in vtables:
        for addr_str in vt.get("entries", []):
            entry_vtable_count[addr_str] += 1

    max_freq = max(entry_vtable_count.values()) if entry_vtable_count else 1
    print(f"  Unique vtable entries: {len(entry_vtable_count):,}")
    print(f"  Max frequency (base class method): {max_freq:,}")
    freq_buckets = Counter()
    for cnt in entry_vtable_count.values():
        if cnt == 1:
            freq_buckets["unique (1)"] += 1
        elif cnt <= 10:
            freq_buckets["rare (2-10)"] += 1
        elif cnt <= 100:
            freq_buckets["common (11-100)"] += 1
        else:
            freq_buckets["inherited (100+)"] += 1
    for bucket, cnt in sorted(freq_buckets.items()):
        print(f"    {bucket}: {cnt:,}")

    # Load CName xrefs for additional vtable identification
    cname_db = load_json(TOOLS_DIR / "cname_xrefs.json")
    cname_vtable_map: dict[str, str] = {}
    cname_func_classes: dict[str, list[str]] = {}
    if cname_db:
        cname_vtable_map = cname_db.get("vtable_class_map", {})
        cname_func_classes = cname_db.get("func_cnames", {})
        print(f"  CName vtable identifications: {len(cname_vtable_map):,}")
        print(f"  CName function class refs: {len(cname_func_classes):,}")

    print("\nIdentifying vtable owner classes...")

    # Sources whose names are regenerable (they're our own or sibling output)
    REGEN_SOURCES = {"vtable_propagated", "vtable_hierarchy"}

    def is_qualified(name: str | None, sources: list | None = None) -> bool:
        if not name:
            return False
        if not ("::" in name and not name.startswith(WEAK_PREFIXES)):
            return False
        # Names sourced entirely from regenerable vtable pipelines are not
        # "qualified" for voting or skip purposes -- they're our own output.
        if sources and set(sources).issubset(REGEN_SOURCES | {"vtable"}):
            return False
        return True

    matches: dict[str, dict] = {}
    vtables_identified = 0
    vtables_skipped = 0

    for vt in vtables:
        entries = vt.get("entries", [])
        if not entries:
            continue

        # Weight votes inversely by how many vtables the entry appears in.
        # An entry unique to this vtable (freq=1) gets weight 1.0.
        # An entry in 1000 vtables gets weight 0.001.
        # Require total weighted vote >= 0.1 to avoid noise from ubiquitous
        # base class methods dominating the identification.
        class_votes: dict[str, float] = defaultdict(float)
        for addr_str in entries:
            info = funcs.get(addr_str, {})
            name = info.get("name") or ""
            if not is_qualified(name, info.get("sources")):
                continue
            cls = name.split("::")[0]
            if ndb_class_set and cls not in ndb_class_set:
                continue
            freq = entry_vtable_count.get(addr_str, 1)
            weight = 1.0 / freq
            class_votes[cls] += weight

        owner_class = None
        if class_votes:
            best = max(class_votes, key=lambda c: class_votes[c])
            if class_votes[best] >= 0.01:
                owner_class = best

        # Fallback: check CName-based vtable identification
        if owner_class is None:
            vt_addr = vt.get("addr")
            if vt_addr and vt_addr in cname_vtable_map:
                cname_cls = cname_vtable_map[vt_addr]
                if cname_cls in ndb_class_set:
                    owner_class = cname_cls

        # Fallback 2: check CName function references for vtable entries
        if owner_class is None and cname_func_classes:
            cname_votes: dict[str, float] = defaultdict(float)
            for addr_str in entries:
                cnames = cname_func_classes.get(addr_str, [])
                for cn in cnames:
                    if cn in ndb_class_set:
                        freq = entry_vtable_count.get(addr_str, 1)
                        cname_votes[cn] += 1.0 / freq
            if cname_votes:
                best_cn = max(cname_votes, key=lambda c: cname_votes[c])
                if cname_votes[best_cn] >= 0.01:
                    owner_class = best_cn

        if owner_class is None:
            vtables_skipped += 1
            continue

        vtables_identified += 1

        ndb_methods = ndb_vtable_map.get(owner_class, [])

        for idx, addr_str in enumerate(entries):
            addr_val = int(addr_str, 16)
            if addr_val % 4 != 0:
                continue

            info = funcs.get(addr_str, {})
            existing_name = info.get("name") or ""

            if is_qualified(existing_name, info.get("sources")):
                continue

            if idx < len(ndb_methods) and ndb_methods[idx] != "?":
                method_name = f"{owner_class}::{ndb_methods[idx]}"
                confidence = "medium"
            else:
                method_name = f"{owner_class}::vmethod[{idx}]"
                confidence = "low"

            matches[addr_str] = {
                "name": method_name,
                "class": owner_class,
                "match_type": "vtable_propagation",
                "confidence": confidence,
                "vtable_addr": vt.get("addr"),
                "vtable_index": idx,
            }

    classes_found = len({m["class"] for m in matches.values()})
    by_confidence = Counter(m["confidence"] for m in matches.values())
    with_method_name = sum(
        1 for m in matches.values() if "::vmethod[" not in m["name"]
    )

    output = {
        "version": "1.0",
        "generated": time.strftime("%Y-%m-%dT%H:%M:%S"),
        "stats": {
            "total_matches": len(matches),
            "vtables_identified": vtables_identified,
            "vtables_skipped": vtables_skipped,
            "classes_found": classes_found,
            "with_method_name": with_method_name,
            "by_confidence": dict(by_confidence),
        },
        "matches": dict(sorted(matches.items())),
    }

    out_path = TOOLS_DIR / "vtable_propagated.json"
    out_path.write_text(json.dumps(output, indent=2))
    elapsed = time.monotonic() - t0

    print(f"\n{'=' * 60}")
    print(f"Wrote {out_path.name} ({out_path.stat().st_size / 1024 / 1024:.1f} MB)")
    print(f"Elapsed: {elapsed:.1f}s")
    print(f"\n  VTables identified: {vtables_identified:,}")
    print(f"  VTables skipped (no class evidence): {vtables_skipped:,}")
    print(f"  Total new name assignments: {len(matches):,}")
    print(f"  With specific method names: {with_method_name:,}")
    print(f"  Classes represented: {classes_found:,}")
    print(f"\n  By confidence:")
    for c, n in sorted(by_confidence.items(), key=lambda kv: -kv[1]):
        print(f"    {c:10s} {n:6,}")


if __name__ == "__main__":
    main()
