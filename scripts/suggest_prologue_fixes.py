#!/usr/bin/env python3
"""
Suggest offset adjustments for failing address entries by scanning nearby ARM64 prologues.

Inputs:
  --binary: Path to Cyberpunk2077 Mach-O binary
  --db:     Path to cyberpunk2077_addresses.json
  --report: Path to scripts/address_validation_report.json (optional)
  --output: Output JSON report path
"""

from __future__ import annotations

import argparse
import json
import struct
from pathlib import Path
from typing import Dict, List, Optional, Tuple

MAGIC_64 = 0xFEEDFACF
LC_SEGMENT_64 = 0x19

SEGMENT_BY_ID = {
    1: "__TEXT",
    2: "__DATA_CONST",
    3: "__DATA",
}


def read_u32_le(buf: bytes, off: int) -> int:
    return struct.unpack_from("<I", buf, off)[0]


def is_pacibsp(instr: int) -> bool:
    return instr == 0xD503237F


def is_stp_fp_lr(instr: int) -> bool:
    return (instr & 0xFF80FFFF) == 0xA9007BFD


def is_stp_x28_x27(instr: int) -> bool:
    return (instr & 0xFF80FFFF) == 0xA9006FFC


def is_sub_sp(instr: int) -> bool:
    return (instr & 0xFF0003FF) == 0xD10003FF


def is_generic_stp(instr: int) -> bool:
    return (instr & 0xFF000000) == 0xA9000000


def is_prologue(instr: int) -> bool:
    return is_pacibsp(instr) or is_stp_fp_lr(instr) or is_stp_x28_x27(instr) or is_sub_sp(instr) or is_generic_stp(instr)


class MachOSegment:
    def __init__(self, name: str, vmaddr: int, vmsize: int, fileoff: int, filesize: int):
        self.name = name
        self.vmaddr = vmaddr
        self.vmsize = vmsize
        self.fileoff = fileoff
        self.filesize = filesize

    def contains_offset(self, offset: int) -> bool:
        return 0 <= offset < self.filesize


class MachOImage:
    def __init__(self, path: Path):
        self.path = path
        self.data = path.read_bytes()
        self.segments: Dict[str, MachOSegment] = {}
        self._parse()

    def _parse(self) -> None:
        if len(self.data) < 32:
            raise RuntimeError("Binary too small")
        magic = read_u32_le(self.data, 0)
        if magic != MAGIC_64:
            raise RuntimeError(f"Unsupported Mach-O magic: 0x{magic:08X}")

        # mach_header_64: magic, cputype, cpusubtype, filetype, ncmds, sizeofcmds, flags, reserved
        _, _, _, _, ncmds, sizeofcmds, _, _ = struct.unpack_from("<IiiIIIII", self.data, 0)
        cmd_off = 32
        for _ in range(ncmds):
            if cmd_off + 8 > len(self.data):
                break
            cmd, cmdsize = struct.unpack_from("<II", self.data, cmd_off)
            if cmdsize == 0:
                break
            if cmd == LC_SEGMENT_64:
                # segment_command_64
                seg_data = self.data[cmd_off: cmd_off + cmdsize]
                if len(seg_data) >= 72:
                    segname = seg_data[8:24].split(b"\x00", 1)[0].decode("utf-8", errors="ignore")
                    vmaddr, vmsize, fileoff, filesize = struct.unpack_from("<QQQQ", seg_data, 24)
                    self.segments[segname] = MachOSegment(segname, vmaddr, vmsize, fileoff, filesize)
            cmd_off += cmdsize

    def read_instr_at(self, seg: MachOSegment, offset: int) -> Optional[int]:
        if not seg.contains_offset(offset):
            return None
        file_off = seg.fileoff + offset
        if file_off + 4 > len(self.data):
            return None
        return read_u32_le(self.data, file_off)


def load_db(path: Path) -> Dict[int, Tuple[int, int]]:
    """Return map hash -> (segment_id, offset)."""
    obj = json.loads(path.read_text())
    out: Dict[int, Tuple[int, int]] = {}
    for entry in obj.get("Addresses", []):
        h = int(entry["hash"])
        seg_s, off_s = entry["offset"].split(":", 1)
        seg = int(seg_s, 0)
        off = int(off_s, 0)
        out[h] = (seg, off)
    return out


def load_report_fail_names(path: Path) -> List[str]:
    obj = json.loads(path.read_text())
    fails = []
    for entry in obj.get("entries", []):
        status = entry.get("status", "")
        if status.startswith("FAIL_"):
            name = entry.get("name")
            if name:
                fails.append(name)
    return fails


def load_name_to_hash(header: Path) -> Dict[str, int]:
    import re
    text = header.read_text()
    pat = re.compile(r'^constexpr\s+std::uint32_t\s+(\w+)\s*=\s*([^;]+);', re.M)
    out: Dict[str, int] = {}
    for name, val in pat.findall(text):
        val = re.sub(r'[UuLl]+$', '', val.strip())
        out[name] = int(val, 0)
    return out


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--binary", required=True, type=Path)
    ap.add_argument("--db", required=True, type=Path)
    ap.add_argument("--report", type=Path)
    ap.add_argument("--header", type=Path, default=Path("/Users/jackmazac/Development/RED4ext/deps/red4ext.sdk/include/RED4ext/Detail/AddressHashes.hpp"))
    ap.add_argument("--output", required=True, type=Path)
    ap.add_argument("--scan-bytes", type=int, default=0x200)
    args = ap.parse_args()

    image = MachOImage(args.binary)
    db = load_db(args.db)
    name_to_hash = load_name_to_hash(args.header)

    fail_names: Optional[List[str]] = None
    if args.report and args.report.exists():
        fail_names = load_report_fail_names(args.report)

    results = []

    for name, h in name_to_hash.items():
        if fail_names is not None and name not in fail_names:
            continue
        if h not in db:
            continue
        seg_id, off = db[h]
        seg_name = SEGMENT_BY_ID.get(seg_id)
        if not seg_name or seg_name not in image.segments:
            results.append({
                "name": name,
                "hash": f"0x{h:08X}",
                "segment": seg_id,
                "offset": f"0x{off:X}",
                "status": "segment_missing",
            })
            continue
        seg = image.segments[seg_name]
        instr = image.read_instr_at(seg, off)
        if instr is None:
            results.append({
                "name": name,
                "hash": f"0x{h:08X}",
                "segment": seg_id,
                "offset": f"0x{off:X}",
                "status": "read_failed",
            })
            continue

        if is_prologue(instr):
            results.append({
                "name": name,
                "hash": f"0x{h:08X}",
                "segment": seg_id,
                "offset": f"0x{off:X}",
                "status": "prologue_ok",
                "instr": f"0x{instr:08X}",
            })
            continue

        # scan backwards for a likely prologue
        found = None
        scan = args.scan_bytes
        for delta in range(4, scan + 1, 4):
            cand_off = off - delta
            if cand_off < 0:
                break
            cand_instr = image.read_instr_at(seg, cand_off)
            if cand_instr is None:
                continue
            if is_prologue(cand_instr):
                found = (cand_off, cand_instr, delta)
                break

        if found:
            cand_off, cand_instr, delta = found
            results.append({
                "name": name,
                "hash": f"0x{h:08X}",
                "segment": seg_id,
                "offset": f"0x{off:X}",
                "status": "suggest",
                "suggested_offset": f"0x{cand_off:X}",
                "delta": -delta,
                "instr": f"0x{instr:08X}",
                "suggested_instr": f"0x{cand_instr:08X}",
            })
        else:
            results.append({
                "name": name,
                "hash": f"0x{h:08X}",
                "segment": seg_id,
                "offset": f"0x{off:X}",
                "status": "no_prologue_found",
                "instr": f"0x{instr:08X}",
            })

    out = {
        "binary": str(args.binary),
        "db": str(args.db),
        "scan_bytes": args.scan_bytes,
        "results": results,
    }

    args.output.write_text(json.dumps(out, indent=2) + "\n")
    print(f"Wrote {args.output} ({len(results)} entries)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
