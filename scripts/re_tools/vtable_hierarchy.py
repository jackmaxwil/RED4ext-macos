#!/usr/bin/env python3
"""
VTable Hierarchy Matching: Identify vtable owner classes by analyzing which
entries are shared with already-identified vtables (indicating inheritance)
and matching against NativeDB class hierarchy.

Bottom-up approach:
1. For each unidentified vtable, find entries shared with identified vtables
2. Shared entries reveal the parent class (inherited base methods)
3. Use NativeDB hierarchy to find candidate child classes of that parent
4. Match candidates by vtable size and unique-entry patterns

Reads:  vtable_database.json, vtable_propagated.json, function_map.json,
        nativedb_data/classes.json, cname_xrefs.json
Writes: vtable_hierarchy.json
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


def build_nativedb_maps(classes: list[dict]) -> tuple[
    dict[str, str | None],
    dict[str, list[str]],
    dict[str, list[str]],
]:
    parents: dict[str, str | None] = {}
    children: dict[str, list[str]] = defaultdict(list)
    func_map: dict[str, list[str]] = {}

    for c in classes:
        name = c.get("b", "")
        if not name:
            continue
        parent = c.get("a") or None
        parents[name] = parent
        if parent:
            children[parent].append(name)

        funcs = c.get("f", [])
        non_static = []
        for fn in funcs:
            flags = fn.get("d", 0)
            if flags & 16:
                continue
            short = fn.get("b") or fn.get("a", "").split(";")[0]
            non_static.append(short or "?")
        func_map[name] = non_static

    return parents, children, func_map


def main():
    print("VTable Hierarchy Matching")
    print("=" * 60)
    t0 = time.monotonic()

    print("\nLoading databases...")
    vt_db = load_json(TOOLS_DIR / "vtable_database.json")
    vt_prop = load_json(TOOLS_DIR / "vtable_propagated.json")
    fm_db = load_json(TOOLS_DIR / "function_map.json")
    ndb_classes = load_json(NATIVEDB_DIR / "classes.json")
    cname_db = load_json(TOOLS_DIR / "cname_xrefs.json")

    if not vt_db or not fm_db:
        print("ERROR: vtable_database.json and function_map.json required")
        return

    vtables = vt_db.get("vtables", [])
    funcs = fm_db.get("functions", {})
    print(f"  VTables: {len(vtables):,}")
    print(f"  Functions: {len(funcs):,}")

    ndb_parents, ndb_children, ndb_func_map = {}, {}, {}
    ndb_class_set: set[str] = set()
    if ndb_classes:
        ndb_parents, ndb_children, ndb_func_map = build_nativedb_maps(ndb_classes)
        ndb_class_set = set(ndb_parents.keys())
        print(f"  NativeDB classes: {len(ndb_class_set):,}")

    # Step 1: Build vtable-to-class mapping from all existing sources
    vt_addr_to_class: dict[str, str] = {}

    if vt_prop:
        for match in vt_prop.get("matches", {}).values():
            vt_addr = match.get("vtable_addr")
            cls = match.get("class")
            if vt_addr and cls and cls in ndb_class_set:
                vt_addr_to_class[vt_addr] = cls

    for vt in vtables:
        vt_addr = vt.get("addr")
        if vt_addr in vt_addr_to_class:
            continue
        class_votes: Counter[str] = Counter()
        for addr_str in vt.get("entries", []):
            info = funcs.get(addr_str, {})
            name = info.get("name") or ""
            sources = info.get("sources", [])
            if "::" in name and not name.startswith(WEAK_PREFIXES):
                if sources != ["vtable_propagated"]:
                    cls = name.split("::")[0]
                    if cls in ndb_class_set:
                        class_votes[cls] += 1
        if class_votes:
            vt_addr_to_class[vt_addr] = class_votes.most_common(1)[0][0]

    if cname_db:
        for vt_addr, cls in cname_db.get("vtable_class_map", {}).items():
            if vt_addr not in vt_addr_to_class and cls in ndb_class_set:
                vt_addr_to_class[vt_addr] = cls

    identified_classes = set(vt_addr_to_class.values())
    print(f"  Pre-identified vtables: {len(vt_addr_to_class):,}")
    print(f"  Unique classes identified: {len(identified_classes):,}")

    # Step 2: Build entry-to-vtable index (which vtables contain each entry)
    print("\nBuilding entry-to-vtable index...")
    entry_to_vts: dict[str, list[str]] = defaultdict(list)
    for vt in vtables:
        vt_addr = vt.get("addr")
        for addr_str in vt.get("entries", []):
            entry_to_vts[addr_str].append(vt_addr)

    # Step 3: For each unidentified vtable, determine parent class
    # by finding which identified vtables share entries with it
    print("Matching unidentified vtables via shared entries...")

    vt_by_addr = {vt.get("addr"): vt for vt in vtables}
    unidentified_addrs = [
        vt.get("addr") for vt in vtables
        if vt.get("addr") not in vt_addr_to_class
    ]
    print(f"  Unidentified vtables: {len(unidentified_addrs):,}")

    matches: dict[str, dict] = {}
    vtables_matched = 0

    for vt_addr in unidentified_addrs:
        vt = vt_by_addr[vt_addr]
        entries = vt.get("entries", [])
        if not entries:
            continue

        # For each entry in this vtable, check which identified vtables
        # also contain it. The class of those identified vtables tells us
        # about our parent/ancestor class.
        ancestor_votes: dict[str, float] = defaultdict(float)
        for addr_str in entries:
            sibling_vts = entry_to_vts.get(addr_str, [])
            for sib_addr in sibling_vts:
                if sib_addr == vt_addr:
                    continue
                sib_cls = vt_addr_to_class.get(sib_addr)
                if sib_cls:
                    # Weight by how specific this sharing is
                    # (fewer shared vtables = more specific signal)
                    weight = 1.0 / len(sibling_vts)
                    ancestor_votes[sib_cls] += weight

        if not ancestor_votes:
            continue

        # The class with the highest weighted vote is likely our parent
        # or the same class. Get the top candidate.
        top_ancestor = max(ancestor_votes, key=lambda c: ancestor_votes[c])

        # Find candidate child classes from NativeDB
        candidate_children = set(ndb_children.get(top_ancestor, []))
        # Also include the ancestor itself (could be the same class)
        candidate_children.add(top_ancestor)
        # Remove already-identified classes
        available = candidate_children - identified_classes

        if not available:
            # All children are taken; try grandchildren
            grandchildren: set[str] = set()
            for child in ndb_children.get(top_ancestor, []):
                grandchildren.update(ndb_children.get(child, []))
            available = grandchildren - identified_classes

        if not available:
            # Fall back: use the ancestor as the class label
            best_cls = top_ancestor
        else:
            # Match by vtable size vs NativeDB function count
            vt_size = len(entries)
            best_cls = min(
                available,
                key=lambda c: abs(len(ndb_func_map.get(c, [])) - vt_size)
            )

        vtables_matched += 1
        identified_classes.add(best_cls)

        ndb_methods = ndb_func_map.get(best_cls, [])
        for idx, addr_str in enumerate(entries):
            addr_val = int(addr_str, 16)
            if addr_val % 4 != 0:
                continue

            info = funcs.get(addr_str, {})
            existing = info.get("name") or ""
            sources = info.get("sources", [])
            if "::" in existing and not existing.startswith(WEAK_PREFIXES):
                if sources != ["vtable_propagated"]:
                    continue

            if idx < len(ndb_methods) and ndb_methods[idx] != "?":
                method_name = f"{best_cls}::{ndb_methods[idx]}"
            else:
                method_name = f"{best_cls}::vmethod[{idx}]"

            matches[addr_str] = {
                "name": method_name,
                "class": best_cls,
                "match_type": "vtable_hierarchy",
                "confidence": "low",
                "vtable_addr": vt_addr,
                "vtable_index": idx,
                "ancestor": top_ancestor,
            }

    classes_found = len({m["class"] for m in matches.values()})
    with_method_name = sum(
        1 for m in matches.values() if "::vmethod[" not in m["name"]
    )

    output = {
        "version": "1.0",
        "generated": time.strftime("%Y-%m-%dT%H:%M:%S"),
        "stats": {
            "total_matches": len(matches),
            "vtables_matched": vtables_matched,
            "vtables_unmatched": len(unidentified_addrs) - vtables_matched,
            "classes_found": classes_found,
            "with_method_name": with_method_name,
        },
        "matches": dict(sorted(matches.items())),
    }

    out_path = TOOLS_DIR / "vtable_hierarchy.json"
    out_path.write_text(json.dumps(output, indent=2))
    elapsed = time.monotonic() - t0

    print(f"\n{'=' * 60}")
    print(f"Wrote {out_path.name} ({out_path.stat().st_size / 1024 / 1024:.1f} MB)")
    print(f"Elapsed: {elapsed:.1f}s")
    print(f"\n  VTables matched: {vtables_matched:,}")
    print(f"  VTables unmatched: {len(unidentified_addrs) - vtables_matched:,}")
    print(f"  Total new name assignments: {len(matches):,}")
    print(f"  With specific method names: {with_method_name:,}")
    print(f"  Classes represented: {classes_found:,}")


if __name__ == "__main__":
    main()
