#!/usr/bin/env python3
"""
NativeDB Cross-Reference: Match Windows RTTI class definitions against
macOS static analysis data (string xrefs, vtables, exported symbols)
to name previously-unnamed functions.

Three matching passes:
  Pass 1 - String XRef:   Match NativeDB class names against string_index
  Pass 2 - VTable Match:  Correlate vtable entries with class names via their string refs
  Pass 3 - Symbol Correl: Parse exported symbol names, map to NativeDB classes

Reads:  nativedb_data/{classes,globals}.json, string_xrefs.json,
        vtable_database.json, symbol_database.json, function_map.json
Writes: nativedb_matches.json
"""

import json
import re
import time
from collections import defaultdict
from dataclasses import dataclass, field
from pathlib import Path

TOOLS_DIR = Path(__file__).parent
NATIVEDB_DIR = TOOLS_DIR / "nativedb_data"
IMAGE_BASE = 0x100000000


def load_json(path: Path) -> dict | list | None:
    if path.exists():
        return json.loads(path.read_text())
    print(f"  WARNING: {path} not found")
    return None


@dataclass
class ClassInfo:
    name: str
    parent: str | None
    children: list[str] = field(default_factory=list)
    depth: int = 0
    func_names: list[str] = field(default_factory=list)
    native_func_count: int = 0
    virtual_func_count: int = 0
    all_func_count: int = 0
    prop_count: int = 0


def build_class_hierarchy(classes: list[dict]) -> dict[str, ClassInfo]:
    """Build hierarchy from NativeDB classes.json.

    Field mapping: b = this class name, a = parent class name (optional).
    """
    tree: dict[str, ClassInfo] = {}

    for c in classes:
        name = c.get("b", "")
        if not name:
            continue
        parent = c.get("a") or None
        funcs = c.get("f", [])
        props = c.get("e", [])

        native_count = 0
        virtual_count = 0
        func_name_list = []

        for fn in funcs:
            short = fn.get("b") or fn.get("a", "").split(";")[0]
            flags = fn.get("d", 0)
            is_static = bool(flags & 16)
            is_native = bool(flags & 2)

            if short:
                func_name_list.append(short)
            if is_native:
                native_count += 1
            if not is_static:
                virtual_count += 1

        tree[name] = ClassInfo(
            name=name,
            parent=parent,
            func_names=func_name_list,
            native_func_count=native_count,
            virtual_func_count=virtual_count,
            all_func_count=len(funcs),
            prop_count=len(props),
        )

    for ci in tree.values():
        if ci.parent and ci.parent in tree:
            tree[ci.parent].children.append(ci.name)

    def compute_depth(name: str, visited: set | None = None) -> int:
        if visited is None:
            visited = set()
        if name in visited:
            return 0
        visited.add(name)
        ci = tree.get(name)
        if not ci or not ci.parent or ci.parent not in tree:
            return 0
        ci.depth = 1 + compute_depth(ci.parent, visited)
        return ci.depth

    for name in tree:
        compute_depth(name)

    return tree


def pass1_string_xref(
    class_tree: dict[str, ClassInfo],
    string_index: dict[str, list[str]],
    func_strings: dict[str, dict],
    existing_named: set[str],
) -> dict[str, dict]:
    """Match NativeDB class names against the string_index.

    Two sub-passes:
      1a. Exact match: class name appears as an exact string literal
      1b. Substring match: class name (>= 8 chars) appears inside a string
          that a function references
    """
    matches: dict[str, dict] = {}
    class_name_set = set(class_tree.keys())

    addr_to_classes: dict[str, list[str]] = defaultdict(list)

    # 1a. Exact match from string_index
    for class_name in class_name_set:
        if class_name in string_index:
            for addr_str in string_index[class_name]:
                addr_to_classes[addr_str].append(class_name)

    # 1b. Substring match: build reverse index from class names that appear
    # inside the string_index keys, then map back to function addresses.
    long_class_names = sorted(
        [cn for cn in class_name_set if len(cn) >= 8],
        key=len, reverse=True,
    )
    for string_val, addr_list in string_index.items():
        if len(string_val) < 8:
            continue
        for cn in long_class_names:
            if cn in string_val and string_val != cn:
                for addr_str in addr_list:
                    if addr_str not in addr_to_classes:
                        addr_to_classes[addr_str].append(cn)
                break

    for addr_str, class_names in addr_to_classes.items():
        if addr_str in existing_named:
            continue

        if len(class_names) == 1:
            cls = class_names[0]
            confidence = "high"
        else:
            best = max(class_names, key=lambda n: class_tree[n].depth)
            cls = best
            confidence = "medium"

        func_info = func_strings.get(addr_str, {})
        all_strings = func_info.get("strings", [])
        method_hint = _guess_method_from_strings(all_strings, cls, class_tree)

        if method_hint:
            name = f"{cls}::{method_hint}"
        else:
            name = f"{cls}::method"

        matches[addr_str] = {
            "name": name,
            "class": cls,
            "match_type": "string_xref",
            "confidence": confidence,
        }

    return matches


def _guess_method_from_strings(
    strings: list[str], class_name: str, class_tree: dict[str, ClassInfo]
) -> str | None:
    ci = class_tree.get(class_name)
    if not ci:
        return None

    func_names_lower = {fn.lower(): fn for fn in ci.func_names}

    for s in strings:
        if s == class_name:
            continue
        sl = s.lower()
        if sl in func_names_lower:
            return func_names_lower[sl]
        for fn_lower, fn_orig in func_names_lower.items():
            if fn_lower in sl or sl in fn_lower:
                return fn_orig

    return None


def pass2_vtable_match(
    class_tree: dict[str, ClassInfo],
    vtables: list[dict],
    string_index: dict[str, list[str]],
    func_strings: dict[str, dict],
    pass1_matches: dict[str, dict],
    existing_named: set[str],
) -> dict[str, dict]:
    """Match vtable entries to NativeDB classes.

    Sources of class evidence for a vtable entry:
      - The entry address was matched to a class in Pass 1
      - The entry address references a class name string (string_index)

    Even a single entry with class evidence assigns the entire vtable.
    """
    matches: dict[str, dict] = {}
    class_name_set = set(class_tree.keys())

    # Build addr -> class set from string_index
    addr_to_class_refs: dict[str, set[str]] = defaultdict(set)
    for class_name in class_name_set:
        if class_name in string_index:
            for addr_str in string_index[class_name]:
                addr_to_class_refs[addr_str].add(class_name)

    # Also incorporate pass1 results: if an address was matched to a class,
    # that's strong evidence for vtable identification
    for addr_str, match_info in pass1_matches.items():
        cls = match_info.get("class")
        if cls and cls in class_name_set:
            addr_to_class_refs[addr_str].add(cls)

    vtable_class_assignments: list[tuple[dict, str, float]] = []

    for vt in vtables:
        entries = vt.get("entries", [])
        if not entries:
            continue

        class_votes: dict[str, int] = defaultdict(int)
        for entry_addr in entries:
            for cls in addr_to_class_refs.get(entry_addr, set()):
                class_votes[cls] += 1

        if not class_votes:
            continue

        best_cls = max(
            class_votes,
            key=lambda c: (class_votes[c], class_tree.get(c, ClassInfo("", None)).depth),
        )
        vote_count = class_votes[best_cls]
        total_entries = len(entries)
        score = vote_count / max(total_entries, 1)

        vtable_class_assignments.append((vt, best_cls, score))

    skip = existing_named | set(pass1_matches.keys())

    for vt, cls, score in vtable_class_assignments:
        ci = class_tree.get(cls)
        if not ci:
            continue

        entries = vt.get("entries", [])
        confidence = "high" if score >= 0.1 else "medium"

        for idx, entry_addr in enumerate(entries):
            if entry_addr in skip or entry_addr in matches:
                continue

            method_name = None
            if ci.func_names and idx < len(ci.func_names):
                method_name = ci.func_names[idx]

            if method_name:
                name = f"{cls}::{method_name}"
            else:
                name = f"{cls}::vmethod[{idx}]"

            matches[entry_addr] = {
                "name": name,
                "class": cls,
                "match_type": "vtable",
                "confidence": confidence,
                "vtable_addr": vt.get("addr"),
                "vtable_index": idx,
            }

    return matches


def pass3_symbol_correlation(
    class_tree: dict[str, ClassInfo],
    symbol_db: dict,
    existing_matches: dict[str, dict],
) -> dict[str, dict]:
    """Parse demangled exported symbol names and map to NativeDB classes.

    Exported symbols use fully-qualified C++ names with namespaces like
    ``red::memory::Pool``, ``game::player::actions::LocomotionBraindance``.
    NativeDB uses short names like ``LocomotionBraindance``.  We match by
    checking each ``::``-delimited part and also by searching for NativeDB
    class names as substrings of symbol name parts.
    """
    matches: dict[str, dict] = {}
    class_name_set = set(class_tree.keys())

    # Pre-sort by length (longest first) so longer matches win
    sorted_names = sorted(class_name_set, key=len, reverse=True)
    # Only consider class names >= 6 chars to avoid false positives
    long_names = [cn for cn in sorted_names if len(cn) >= 6]

    for entry in symbol_db.get("text_symbols", []):
        addr_str = entry["addr"]
        if addr_str in existing_matches:
            continue

        demangled = entry.get("name", "")
        if not demangled or "::" not in demangled:
            continue

        parts = demangled.split("::")
        found_class = None
        found_method = None

        # Strategy 1: exact match on any part (stripping template args)
        for i, part in enumerate(parts):
            clean = re.sub(r"<.*>", "", part).strip()
            if clean in class_name_set:
                found_class = clean
                if i + 1 < len(parts):
                    found_method = parts[i + 1].split("(")[0].strip()
                break

        # Strategy 2: check if any NativeDB class name appears inside
        # any part of the demangled name (catches namespace-prefixed names)
        if not found_class:
            for cn in long_names:
                for i, part in enumerate(parts):
                    clean = re.sub(r"<.*>", "", part).strip()
                    if cn in clean and len(cn) >= len(clean) * 0.5:
                        found_class = cn
                        if i + 1 < len(parts):
                            found_method = parts[i + 1].split("(")[0].strip()
                        break
                if found_class:
                    break

        # Strategy 3: check if a NativeDB class name appears as a suffix
        # of any namespace-qualified part (e.g. "gameVehicleComponent" ends
        # with "VehicleComponent")
        if not found_class:
            for cn in long_names:
                for i, part in enumerate(parts):
                    clean = re.sub(r"<.*>", "", part).strip()
                    if clean.endswith(cn) and len(clean) <= len(cn) + 20:
                        found_class = cn
                        if i + 1 < len(parts):
                            found_method = parts[i + 1].split("(")[0].strip()
                        break
                if found_class:
                    break

        if found_class:
            ci = class_tree.get(found_class)
            if found_method and ci:
                actual = found_method
                for fn in ci.func_names:
                    if fn.lower() == found_method.lower():
                        actual = fn
                        break
                name = f"{found_class}::{actual}"
                confidence = "high"
            else:
                name = f"{found_class}::symbol"
                confidence = "medium"

            matches[addr_str] = {
                "name": name,
                "class": found_class,
                "match_type": "symbol",
                "confidence": confidence,
            }

    return matches


def main():
    print("NativeDB Cross-Reference Tool")
    print("=" * 60)
    t0 = time.monotonic()

    print("\nLoading NativeDB data...")
    ndb_classes = load_json(NATIVEDB_DIR / "classes.json")
    ndb_globals = load_json(NATIVEDB_DIR / "globals.json")
    if not ndb_classes:
        print("ERROR: classes.json not found in nativedb_data/")
        return

    print(f"  Classes: {len(ndb_classes)}")
    if ndb_globals:
        print(f"  Globals: {len(ndb_globals)}")

    print("\nBuilding class hierarchy...")
    class_tree = build_class_hierarchy(ndb_classes)
    print(f"  Classes in tree: {len(class_tree)}")
    root_classes = [c for c in class_tree.values() if not c.parent or c.parent not in class_tree]
    max_depth = max((c.depth for c in class_tree.values()), default=0)
    print(f"  Root classes: {len(root_classes)}")
    print(f"  Max depth: {max_depth}")

    print("\nLoading macOS analysis databases...")
    xref_db = load_json(TOOLS_DIR / "string_xrefs.json")
    vtable_db = load_json(TOOLS_DIR / "vtable_database.json")
    symbol_db = load_json(TOOLS_DIR / "symbol_database.json")
    func_map_db = load_json(TOOLS_DIR / "function_map.json")

    string_index: dict[str, list[str]] = {}
    func_strings: dict[str, dict] = {}
    if xref_db:
        string_index = xref_db.get("string_index", {})
        func_strings = xref_db.get("functions", {})
        print(f"  String index: {len(string_index)} strings")
        print(f"  Functions with strings: {len(func_strings)}")

    vtables: list[dict] = []
    if vtable_db:
        vtables = vtable_db.get("vtables", [])
        print(f"  VTables: {len(vtables)}")

    if symbol_db:
        print(f"  Exported symbols: {len(symbol_db.get('text_symbols', []))}")

    existing_named: set[str] = set()
    if func_map_db:
        for addr_str, info in func_map_db.get("functions", {}).items():
            if isinstance(info, dict) and info.get("name"):
                n = info["name"]
                if not n.startswith("ref:") and not n.startswith("str:") and not n.startswith("called_by:"):
                    existing_named.add(addr_str)
        print(f"  Already well-named functions: {len(existing_named)}")

    all_matches: dict[str, dict] = {}

    print("\n--- Pass 1: String XRef Matching ---")
    p1 = pass1_string_xref(class_tree, string_index, func_strings, existing_named)
    print(f"  Matched: {len(p1)} functions")
    all_matches.update(p1)

    print("\n--- Pass 2: VTable Structure Matching ---")
    p2 = pass2_vtable_match(class_tree, vtables, string_index, func_strings, p1, existing_named)
    print(f"  Matched: {len(p2)} functions")
    all_matches.update(p2)

    print("\n--- Pass 3: Symbol Name Correlation ---")
    if symbol_db:
        skip_set = set(all_matches.keys()) | existing_named
        p3 = pass3_symbol_correlation(class_tree, symbol_db, {a: {} for a in skip_set})
        print(f"  Matched: {len(p3)} functions")
        all_matches.update(p3)
    else:
        p3 = {}

    classes_matched = len({m["class"] for m in all_matches.values()})
    by_method = defaultdict(int)
    by_confidence = defaultdict(int)
    for m in all_matches.values():
        by_method[m["match_type"]] += 1
        by_confidence[m["confidence"]] += 1

    output = {
        "version": "1.0",
        "generated": time.strftime("%Y-%m-%dT%H:%M:%S"),
        "stats": {
            "total_matches": len(all_matches),
            "classes_matched": classes_matched,
            "by_method": dict(by_method),
            "by_confidence": dict(by_confidence),
        },
        "matches": dict(sorted(all_matches.items())),
    }

    out_path = TOOLS_DIR / "nativedb_matches.json"
    out_path.write_text(json.dumps(output, indent=2))
    elapsed = time.monotonic() - t0

    print(f"\n{'=' * 60}")
    print(f"Wrote {out_path.name} ({out_path.stat().st_size / 1024:.0f} KB)")
    print(f"Elapsed: {elapsed:.1f}s")
    print(f"\n  Total new matches: {len(all_matches):,}")
    print(f"  Classes matched: {classes_matched:,}")
    print(f"\n  By method:")
    for m, c in sorted(by_method.items(), key=lambda kv: -kv[1]):
        print(f"    {m:15s} {c:6,}")
    print(f"\n  By confidence:")
    for c, n in sorted(by_confidence.items(), key=lambda kv: -kv[1]):
        print(f"    {c:10s} {n:6,}")


if __name__ == "__main__":
    main()
