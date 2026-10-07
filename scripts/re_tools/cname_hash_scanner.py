#!/usr/bin/env python3
"""
CName Hash Scanner: Find NativeDB class/function names in the binary by
scanning data sections for FNV1a64 hash values and tracing ADRP+LDR pairs
that load them.

1. Compute FNV1a64 hashes for all NativeDB class + global function names
2. Scan __TEXT.__const, __DATA_CONST.__const, __DATA.__data for matching 8-byte values
3. Scan __text for ADRP+LDR pairs that load from matched data addresses
4. Map each load back to its containing function
5. Cross-reference with vtable entries to identify more vtables

Reads:  nativedb_data/{classes,globals}.json, vtable_database.json, game binary
Writes: cname_xrefs.json
"""

import json
import mmap
import struct
import subprocess
import sys
import time
from collections import Counter, defaultdict
from pathlib import Path

TOOLS_DIR = Path(__file__).parent
NATIVEDB_DIR = TOOLS_DIR / "nativedb_data"
IMAGE_BASE = 0x100000000

DEFAULT_BINARY = (
    Path.home()
    / "Library/Application Support/Steam/steamapps/common"
    / "Cyberpunk 2077/Cyberpunk2077.app/Contents/MacOS/Cyberpunk2077"
)


def fnv1a64(text: str) -> int:
    h = 0xCBF29CE484222325
    for ch in text.encode("utf-8"):
        h ^= ch
        h = (h * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return h


def load_json(path: Path) -> dict | list | None:
    if path.exists():
        return json.loads(path.read_text())
    return None


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
                sections[f"{info.get('seg', '?')}.{current_sect}"] = info
            current_sect = s.split()[-1]
            info = {"seg": current_seg}
        elif s.startswith("addr "):
            info["addr"] = int(s.split()[-1], 16)
        elif s.startswith("size ") and "align" not in s:
            info["size"] = int(s.split()[-1], 16)
        elif s.startswith("offset ") and "reloff" not in s:
            info["offset"] = int(s.split()[-1])
    if current_sect and info:
        sections[f"{info.get('seg', '?')}.{current_sect}"] = info
    return sections


def decode_adrp(instr: int, pc: int) -> tuple | None:
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


def decode_ldr_imm64(instr: int) -> tuple | None:
    """LDR Xt, [Xn, #imm] -- 64-bit unsigned offset."""
    if (instr & 0xFFC00000) != 0xF9400000:
        return None
    rt = instr & 0x1F
    rn = (instr >> 5) & 0x1F
    imm12 = (instr >> 10) & 0xFFF
    return (rt, rn, imm12 << 3)


def find_function_start(text_data: bytes, offset_in_text: int) -> int:
    search_start = max(0, offset_in_text - 4096)
    pos = (offset_in_text // 4) * 4
    while pos >= search_start:
        instr = struct.unpack_from("<I", text_data, pos)[0]
        if (instr & 0xFF0003FF) == 0xD10003FF:
            return pos
        if (instr & 0xFFC003E0) == 0xA98003E0:
            return pos
        pos -= 4
    return offset_in_text


def main():
    binary = str(Path(sys.argv[1]) if len(sys.argv) > 1 else DEFAULT_BINARY)

    print("CName Hash Scanner")
    print("=" * 60)
    t0 = time.monotonic()

    print("\nLoading NativeDB data...")
    ndb_classes = load_json(NATIVEDB_DIR / "classes.json")
    ndb_globals = load_json(NATIVEDB_DIR / "globals.json")
    if not ndb_classes:
        print("ERROR: classes.json required")
        return

    # Step 1: Compute FNV1a64 hashes for all class + global + method names
    print("Computing FNV1a64 hashes...")
    hash_to_name: dict[int, str] = {}
    method_hash_to_names: dict[int, list[str]] = defaultdict(list)
    class_to_methods: dict[str, set[str]] = defaultdict(set)

    for c in ndb_classes:
        name = c.get("b", "")
        if name:
            h = fnv1a64(name)
            hash_to_name[h] = name
            for fn in c.get("f", []):
                method = fn.get("b") or fn.get("a", "").split(";")[0]
                if method:
                    class_to_methods[name].add(method)

    if ndb_globals:
        for g in ndb_globals:
            name = g.get("a", "").split(";")[0]
            if name:
                h = fnv1a64(name)
                hash_to_name[h] = f"global::{name}"

    # Hash method names separately (many methods share the same name
    # across classes, so map hash -> list of method names)
    all_methods: set[str] = set()
    for methods in class_to_methods.values():
        all_methods.update(methods)
    for method in all_methods:
        h = fnv1a64(method)
        method_hash_to_names[h].append(method)

    print(f"  Class/global hashes: {len(hash_to_name):,}")
    print(f"  Unique method names: {len(all_methods):,}")
    print(f"  Unique method hashes: {len(method_hash_to_names):,}")

    # Convert to set of 8-byte packed values for fast scanning
    target_bytes: dict[bytes, str] = {}
    for h, name in hash_to_name.items():
        packed = struct.pack("<Q", h)
        target_bytes[packed] = name

    # Step 2: Parse sections and scan data for hash values
    print(f"\nParsing sections from {Path(binary).name}...")
    sections = parse_sections(binary)

    data_sections = [
        ("__TEXT.__const", sections.get("__TEXT.__const")),
        ("__DATA_CONST.__const", sections.get("__DATA_CONST.__const")),
        ("__DATA.__data", sections.get("__DATA.__data")),
    ]

    # data_addr -> class/function name
    hash_locations: dict[int, str] = {}

    with open(binary, "rb") as f:
        mm = mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_READ)

        for sect_name, sect_info in data_sections:
            if not sect_info:
                continue
            addr = sect_info["addr"]
            offset = sect_info["offset"]
            size = sect_info["size"]
            print(f"  Scanning {sect_name}: 0x{addr:X} ({size / 1024 / 1024:.1f} MB)")

            data = mm[offset: offset + size]
            found = 0
            # Scan on 8-byte alignment for CName hashes
            for i in range(0, size - 7, 8):
                chunk = data[i:i + 8]
                if chunk in target_bytes:
                    data_addr = addr + i
                    hash_locations[data_addr] = target_bytes[chunk]
                    found += 1
            print(f"    Found: {found} hash matches")

        # Step 3: Scan __text for ADRP+LDR pairs targeting hash locations
        text = sections["__TEXT.__text"]
        text_addr = text["addr"]
        text_offset = text["offset"]
        text_size = text["size"]

        print(f"\nScanning __text for ADRP+LDR pairs ({text_size / 1024 / 1024:.1f} MB)...")
        text_data = mm[text_offset: text_offset + text_size]

        adrp_regs: dict[int, tuple[int, int]] = {}
        code_refs: list[tuple[int, int, str]] = []  # (instr_offset, data_addr, name)

        for i in range(0, text_size - 4, 4):
            instr = struct.unpack_from("<I", text_data, i)[0]
            pc = text_addr + i

            adrp = decode_adrp(instr, pc)
            if adrp is not None:
                rd, page = adrp
                adrp_regs[rd] = (page, i)
                continue

            ldr = decode_ldr_imm64(instr)
            if ldr is not None:
                rt, rn, imm = ldr
                if rn in adrp_regs:
                    page, adrp_off = adrp_regs[rn]
                    if (i - adrp_off) <= 64:
                        target = page + imm
                        if target in hash_locations:
                            code_refs.append((i, target, hash_locations[target]))

        print(f"  ADRP+LDR references to CName hashes: {len(code_refs):,}")

        # Step 3b: Scan for MOV+MOVK sequences that build CName hashes.
        # ARM64 builds 64-bit immediates with up to 4 instructions:
        #   MOVZ Xd, #imm16           -> sets Xd = imm16
        #   MOVK Xd, #imm16, LSL #16 -> keeps other bits, sets bits 16-31
        #   MOVK Xd, #imm16, LSL #32
        #   MOVK Xd, #imm16, LSL #48
        print("Scanning for MOV+MOVK immediate sequences...")

        hash_by_low16: dict[int, list[int]] = defaultdict(list)
        for h in hash_to_name:
            low16 = h & 0xFFFF
            hash_by_low16[low16].append(h)

        movk_refs: list[tuple[int, str]] = []

        for i in range(0, text_size - 16, 4):
            instr0 = struct.unpack_from("<I", text_data, i)[0]

            # MOVZ Xd, #imm16 (LSL #0): 1_10_100101_00_imm16_Rd
            if (instr0 & 0xFFE00000) != 0xD2800000:
                continue

            rd = instr0 & 0x1F
            imm16_0 = (instr0 >> 5) & 0xFFFF
            candidates = hash_by_low16.get(imm16_0)
            if not candidates:
                continue

            built_value = imm16_0
            shifts_seen = {0}

            for j in range(1, 4):
                off = i + j * 4
                if off + 4 > text_size:
                    break
                instr_j = struct.unpack_from("<I", text_data, off)[0]
                # MOVK Xd, #imm16, LSL #N: 1_11_100101_hw_imm16_Rd
                if (instr_j & 0x1F) != rd:
                    break
                hw = (instr_j >> 21) & 0x3
                opc = instr_j & 0xFF800000
                # MOVK with hw=0: 0xF2800000, hw=1: 0xF2A00000,
                # hw=2: 0xF2C00000, hw=3: 0xF2E00000
                expected = 0xF2800000 | (hw << 21)
                if opc != (expected >> 23) << 23:
                    # More precise: check top 9 bits match MOVK
                    if (instr_j & 0xFF800000) not in (
                        0xF2800000, 0xF2A00000, 0xF2C00000, 0xF2E00000
                    ):
                        break
                shift = hw * 16
                if shift in shifts_seen:
                    break
                shifts_seen.add(shift)
                imm16_j = (instr_j >> 5) & 0xFFFF
                built_value |= (imm16_j << shift)

            if len(shifts_seen) >= 3 and built_value in hash_to_name:
                name = hash_to_name[built_value]
                movk_refs.append((i, name))

        print(f"  MOV+MOVK class/global hash constructions: {len(movk_refs):,}")

        # Step 3c: Scan for MOV+MOVK sequences building METHOD name hashes
        print("Scanning for method-name MOV+MOVK sequences...")

        method_hash_by_low16: dict[int, list[int]] = defaultdict(list)
        for h in method_hash_to_names:
            low16 = h & 0xFFFF
            method_hash_by_low16[low16].append(h)

        method_movk_refs: list[tuple[int, str]] = []

        for i in range(0, text_size - 16, 4):
            instr0 = struct.unpack_from("<I", text_data, i)[0]

            if (instr0 & 0xFFE00000) != 0xD2800000:
                continue

            rd = instr0 & 0x1F
            imm16_0 = (instr0 >> 5) & 0xFFFF
            candidates = method_hash_by_low16.get(imm16_0)
            if not candidates:
                continue

            built_value = imm16_0
            shifts_seen = {0}

            for j in range(1, 4):
                off = i + j * 4
                if off + 4 > text_size:
                    break
                instr_j = struct.unpack_from("<I", text_data, off)[0]
                if (instr_j & 0x1F) != rd:
                    break
                if (instr_j & 0xFF800000) not in (
                    0xF2800000, 0xF2A00000, 0xF2C00000, 0xF2E00000
                ):
                    break
                hw = (instr_j >> 21) & 0x3
                shift = hw * 16
                if shift in shifts_seen:
                    break
                shifts_seen.add(shift)
                imm16_j = (instr_j >> 5) & 0xFFFF
                built_value |= (imm16_j << shift)

            if len(shifts_seen) >= 3 and built_value in method_hash_to_names:
                names = method_hash_to_names[built_value]
                method_movk_refs.append((i, names[0]))

        print(f"  MOV+MOVK method hash constructions: {len(method_movk_refs):,}")

        mm.close()

    # Step 4: Map references to containing functions
    print("\nMapping to containing functions...")
    func_cnames: dict[int, list[str]] = defaultdict(list)

    for instr_off, data_addr, name in code_refs:
        func_off = find_function_start(text_data, instr_off)
        func_addr = text_addr + func_off
        func_cnames[func_addr].append(name)

    for instr_off, name in movk_refs:
        func_off = find_function_start(text_data, instr_off)
        func_addr = text_addr + func_off
        func_cnames[func_addr].append(name)

    # Map method hash refs to functions
    func_methods: dict[int, list[str]] = defaultdict(list)
    for instr_off, method in method_movk_refs:
        func_off = find_function_start(text_data, instr_off)
        func_addr = text_addr + func_off
        func_methods[func_addr].append(method)

    for addr in func_methods:
        func_methods[addr] = sorted(set(func_methods[addr]))

    # Deduplicate class refs
    for addr in func_cnames:
        func_cnames[addr] = sorted(set(func_cnames[addr]))

    print(f"  {len(func_cnames):,} unique functions reference class CName hashes")
    print(f"  {len(func_methods):,} unique functions reference method CName hashes")

    # Step 4b: Cross-reference class + method hashes within same function
    # If a function builds both a class CName and a method CName, and that
    # method is valid for that class, we have a strong Class::Method match.
    func_class_method: dict[int, tuple[str, str]] = {}
    both_count = 0
    for addr in func_cnames:
        if addr not in func_methods:
            continue
        classes = [c for c in func_cnames[addr] if not c.startswith("global::")]
        methods = func_methods[addr]
        if not classes or not methods:
            continue
        both_count += 1
        for cls in classes:
            cls_methods = class_to_methods.get(cls, set())
            for method in methods:
                if method in cls_methods:
                    func_class_method[addr] = (cls, method)
                    break
            if addr in func_class_method:
                break

    print(f"  Functions with both class+method: {both_count:,}")
    print(f"  Confirmed Class::Method pairs: {len(func_class_method):,}")

    # Step 5: Cross-reference with vtable entries
    print("\nCross-referencing with vtable entries...")
    vt_db = load_json(TOOLS_DIR / "vtable_database.json")
    vtable_entries_to_vtable: dict[str, list[str]] = defaultdict(list)
    if vt_db:
        for vt in vt_db.get("vtables", []):
            vt_addr = vt.get("addr")
            for entry in vt.get("entries", []):
                vtable_entries_to_vtable[entry].append(vt_addr)

    vtable_class_map: dict[str, str] = {}
    for func_addr, cnames in func_cnames.items():
        addr_str = f"0x{func_addr:X}"
        if addr_str in vtable_entries_to_vtable:
            for vt_addr in vtable_entries_to_vtable[addr_str]:
                # First non-global class name wins
                for cn in cnames:
                    if not cn.startswith("global::"):
                        if vt_addr not in vtable_class_map:
                            vtable_class_map[vt_addr] = cn
                        break

    print(f"  VTables newly identified via CName: {len(vtable_class_map):,}")

    # Build output
    classes_seen = set()
    for names in func_cnames.values():
        for n in names:
            if not n.startswith("global::"):
                classes_seen.add(n)

    by_source = Counter()
    for data_addr, name in hash_locations.items():
        if data_addr < sections.get("__TEXT.__const", {}).get("addr", 0) + sections.get("__TEXT.__const", {}).get("size", 0):
            by_source["__TEXT.__const"] += 1
        elif data_addr < sections.get("__DATA_CONST.__const", {}).get("addr", 0) + sections.get("__DATA_CONST.__const", {}).get("size", 0):
            by_source["__DATA_CONST.__const"] += 1
        else:
            by_source["__DATA.__data"] += 1

    output = {
        "version": "2.0",
        "generated": time.strftime("%Y-%m-%dT%H:%M:%S"),
        "stats": {
            "class_hashes_computed": len(hash_to_name),
            "method_hashes_computed": len(method_hash_to_names),
            "hash_locations_found": len(hash_locations),
            "code_references_adrp_ldr": len(code_refs),
            "class_movk_refs": len(movk_refs),
            "method_movk_refs": len(method_movk_refs),
            "functions_referencing_classes": len(func_cnames),
            "functions_referencing_methods": len(func_methods),
            "confirmed_class_method_pairs": len(func_class_method),
            "classes_referenced": len(classes_seen),
            "vtables_identified": len(vtable_class_map),
            "by_data_section": dict(by_source),
        },
        "func_cnames": {
            f"0x{addr:X}": names
            for addr, names in sorted(func_cnames.items())
        },
        "func_methods": {
            f"0x{addr:X}": methods
            for addr, methods in sorted(func_methods.items())
        },
        "func_class_method": {
            f"0x{addr:X}": {"class": cls, "method": method}
            for addr, (cls, method) in sorted(func_class_method.items())
        },
        "vtable_class_map": vtable_class_map,
        "hash_locations": {
            f"0x{addr:X}": name
            for addr, name in sorted(hash_locations.items())
        },
    }

    out_path = TOOLS_DIR / "cname_xrefs.json"
    out_path.write_text(json.dumps(output, indent=2))
    elapsed = time.monotonic() - t0

    print(f"\n{'=' * 60}")
    print(f"Wrote {out_path.name} ({out_path.stat().st_size / 1024 / 1024:.1f} MB)")
    print(f"Elapsed: {elapsed:.1f}s")
    print(f"\n  Hash values found in data sections: {len(hash_locations):,}")
    print(f"  Class MOV+MOVK refs: {len(movk_refs):,}")
    print(f"  Method MOV+MOVK refs: {len(method_movk_refs):,}")
    print(f"  Functions referencing class CNames: {len(func_cnames):,}")
    print(f"  Functions referencing method CNames: {len(func_methods):,}")
    print(f"  Confirmed Class::Method pairs: {len(func_class_method):,}")
    print(f"  Classes referenced in code: {len(classes_seen):,}")
    print(f"  VTables newly identified: {len(vtable_class_map):,}")


if __name__ == "__main__":
    main()
