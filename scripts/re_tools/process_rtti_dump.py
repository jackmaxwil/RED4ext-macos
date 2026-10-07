#!/usr/bin/env python3
"""
Phase B post-processor: Resolve CName hashes in the Frida RTTI dump.

CName is a 64-bit FNV1a hash of the string name. This script builds a
hash-to-name lookup from:
  1. Known RTTI type name strings extracted from the binary
  2. Exported symbol names
  3. Known hash constants from RED4ext.SDK

Then applies the lookup to the raw rtti_dump.json, producing a fully-named
rtti_database.json suitable for address resolution.
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


def fnv1a64(text: str) -> int:
    """FNV1a 64-bit hash matching RED4ext CName implementation."""
    h = 0xCBF29CE484222325
    for ch in text.encode("utf-8"):
        h ^= ch
        h = (h * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return h


def build_name_table(binary_path: str) -> dict[str, str]:
    """Build CName hash -> string lookup from binary strings."""
    print("Building CName hash lookup table...")
    t0 = time.monotonic()

    table: dict[str, str] = {}

    # Source 1: All strings from the binary that look like type/function names
    result = subprocess.run(
        ["strings", binary_path], capture_output=True, text=True
    )
    candidates = set()
    for line in result.stdout.splitlines():
        s = line.strip()
        if not s or len(s) > 200:
            continue
        # RTTI type names (camelCase starting with lowercase)
        if re.match(r"^[a-z]{1,}[A-Z]", s) and " " not in s and len(s) < 80:
            candidates.add(s)
        # PascalCase names
        if re.match(r"^[A-Z][a-zA-Z0-9]+$", s) and len(s) > 2:
            candidates.add(s)
        # Namespace::Name patterns
        if "::" in s and len(s) < 100 and " " not in s:
            candidates.add(s)
        # Simple identifiers that could be function/property names
        if re.match(r"^[a-zA-Z_][a-zA-Z0-9_]*$", s) and 2 < len(s) < 60:
            candidates.add(s)

    for name in candidates:
        h = fnv1a64(name)
        table[format(h, "x")] = name

    # Source 2: Common known names from the modding community
    known_names = [
        "CRTTISystem", "CClass", "CBaseRTTIType", "CEnum", "CBitfield",
        "CProperty", "CName", "CString", "CGlobalFunction", "CClassFunction",
        "CClassStaticFunction", "ISerializable", "IScriptable", "IComponent",
        "IGameSystem", "IWorldSystem", "TweakDB", "CNamePool", "ResourceDepot",
        "ScriptStack", "CStack", "ScriptStackFrame", "CBaseFunction",
        "GetName", "ToString", "Get", "Set", "Execute", "Init", "Shutdown",
        "OnAttach", "OnDetach", "OnUpdate", "OnInitialize",
        "isNative", "isStatic", "isEvent", "isCallback", "isTimer",
        "Handle", "WeakHandle", "SharedPtr", "DynArray", "HashMap",
        "Bool", "Int8", "Int16", "Int32", "Int64", "Uint8", "Uint16",
        "Uint32", "Uint64", "Float", "Double", "String", "CName",
        "Vector2", "Vector3", "Vector4", "Quaternion", "Matrix",
        "Transform", "EulerAngles", "Color", "HDRColor",
        "NodeRef", "TweakDBID", "gamedataLocKeyWrapper",
        "ref", "wref", "array", "handle", "whandle",
    ]
    for name in known_names:
        h = fnv1a64(name)
        table[format(h, "x")] = name

    print(f"  {len(table)} entries in {time.monotonic()-t0:.1f}s")
    return table


def resolve_hash(h: str, table: dict[str, str]) -> str:
    """Resolve a CName hash string to a name, or return the hash."""
    if h in table:
        return table[h]
    return f"hash_{h}"


def process_dump(dump_path: Path, table: dict[str, str], out_path: Path):
    """Process raw RTTI dump and resolve all CName hashes."""
    print(f"Processing {dump_path}...")

    dump = json.loads(dump_path.read_text())
    stats = dump.get("stats", {})

    resolved_types = {}
    func_addresses = {}
    type_hierarchy = {}

    for key, tdata in dump.get("types", {}).items():
        name_hash = tdata.get("nameHash", "")
        type_name = resolve_hash(name_hash, table)

        resolved = {
            "name": type_name,
            "addr": tdata.get("addr"),
            "classSize": tdata.get("classSize"),
            "parentAddr": tdata.get("parentAddr"),
            "funcCount": len(tdata.get("functions", [])),
            "propCount": len(tdata.get("properties", [])),
        }

        # Resolve function names
        resolved["functions"] = []
        for fn in tdata.get("functions", []):
            fname = resolve_hash(fn.get("fullNameHash", ""), table)
            sname = resolve_hash(fn.get("shortNameHash", ""), table)
            entry = {
                "fullName": fname,
                "shortName": sname,
                "addr": fn.get("addr"),
                "isNative": fn.get("isNative", False),
            }
            resolved["functions"].append(entry)
            if fn.get("addr"):
                func_addresses[fn["addr"]] = f"{type_name}::{sname}"

        # Resolve property names
        resolved["properties"] = []
        for prop in tdata.get("properties", []):
            pname = resolve_hash(prop.get("nameHash", ""), table)
            resolved["properties"].append({
                "name": pname,
                "offset": prop.get("offset"),
            })

        resolved_types[type_name] = resolved

    # Resolve global functions
    resolved_globals = {}
    for key, fdata in dump.get("globalFunctions", {}).items():
        fname = resolve_hash(fdata.get("fullNameHash", ""), table)
        sname = resolve_hash(fdata.get("shortNameHash", ""), table)
        resolved_globals[fname] = {
            "fullName": fname,
            "shortName": sname,
            "addr": fdata.get("addr"),
            "isNative": fdata.get("isNative", False),
        }
        if fdata.get("addr"):
            func_addresses[fdata["addr"]] = f"global::{fname}"

    # Build parent hierarchy
    addr_to_name = {}
    for tname, tdata in resolved_types.items():
        if tdata.get("addr"):
            addr_to_name[tdata["addr"]] = tname

    for tname, tdata in resolved_types.items():
        parent_addr = tdata.get("parentAddr")
        if parent_addr and parent_addr in addr_to_name:
            tdata["parent"] = addr_to_name[parent_addr]
        del tdata["parentAddr"]

    # Count resolution stats
    named = sum(1 for n in resolved_types if not n.startswith("hash_"))
    unnamed = sum(1 for n in resolved_types if n.startswith("hash_"))
    gnamed = sum(1 for n in resolved_globals if not n.startswith("hash_"))

    output = {
        "version": "2.0",
        "generated": time.strftime("%Y-%m-%dT%H:%M:%S"),
        "source": str(dump_path),
        "stats": {
            "types": len(resolved_types),
            "types_named": named,
            "types_unresolved": unnamed,
            "globalFunctions": len(resolved_globals),
            "globalFunctions_named": gnamed,
            "totalFunctionAddresses": len(func_addresses),
            "totalProperties": sum(
                len(t.get("properties", [])) for t in resolved_types.values()
            ),
        },
        "types": dict(sorted(resolved_types.items())),
        "globalFunctions": dict(sorted(resolved_globals.items())),
        "functionAddressMap": dict(sorted(func_addresses.items())),
    }

    out_path.write_text(json.dumps(output, indent=2))
    print(f"\nWrote {out_path} ({out_path.stat().st_size / 1024 / 1024:.1f} MB)")
    print(f"Stats:")
    for k, v in output["stats"].items():
        print(f"  {k}: {v}")


def run():
    dump_path = Path(sys.argv[1]) if len(sys.argv) > 1 else Path("/tmp/rtti_dump.json")
    binary = str(Path(sys.argv[2]) if len(sys.argv) > 2 else DEFAULT_BINARY)
    out_path = Path(__file__).parent / "rtti_database.json"

    if not dump_path.exists():
        print(f"RTTI dump not found at {dump_path}")
        print("Run rtti_dumper_with_names.js via Frida first, then re-run this script.")
        print("\nCreating placeholder with hash table for future use...")
        table = build_name_table(binary)
        table_path = Path(__file__).parent / "cname_hash_table.json"
        table_path.write_text(json.dumps(
            {"version": "1.0", "count": len(table), "hashes": table},
            indent=2,
        ))
        print(f"Wrote {table_path} ({table_path.stat().st_size / 1024:.0f} KB)")
        return

    table = build_name_table(binary)
    process_dump(dump_path, table, out_path)


if __name__ == "__main__":
    run()
