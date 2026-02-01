#!/usr/bin/env python3
"""
Merge Results Script - Combine Static + Dynamic Analysis

Combines static and dynamic analysis results into a unified evidence dataset,
then generates an SDK-compatible `cyberpunk2077_addresses.json` using a manual
name->address mapping file.

Usage:
    python3 merge_results.py \
      --static static_functions.json \
      --dynamic dynamic_functions.json \
      --manual ../manual_addresses_template.json \
      --output cyberpunk2077_addresses.json \
      --report merge_report.json
"""

from __future__ import annotations

import argparse
import json
import re
import sys
import time
from pathlib import Path
from typing import Any, Dict, Iterable, List, Optional, Tuple

# Import shared data structures
try:
    from re_common import FunctionInfo, CallGraphEdge, IMAGE_BASE
except ImportError:
    sys.path.insert(0, str(Path(__file__).parent))
    from re_common import FunctionInfo, CallGraphEdge, IMAGE_BASE


HashConstants = Dict[str, int]  # name -> u32 hash


HASH_CONSTANT_PATTERN = re.compile(
    r"constexpr\s+std::uint32_t\s+(\w+)\s*=\s*(0x[0-9A-Fa-f]+|\d+)[UuLl]*\s*;"
)


def _load_json(path: Path) -> Dict[str, Any]:
    with open(path, "r") as f:
        return json.load(f)


def _parse_analysis_type(data: Dict[str, Any]) -> Optional[str]:
    # v1: { "analysis_type": "dynamic" }
    # v2: { "metadata": { "analysis_type": "dynamic" } }
    return (
        data.get("analysis_type")
        or data.get("metadata", {}).get("analysis_type")
        or data.get("metadata", {}).get("analysisType")
    )


def load_static_results(static_json_path: Path) -> Dict[str, Any]:
    """Load static analysis JSON."""
    print(f"[*] Loading static results from {static_json_path}...")
    
    if not static_json_path.exists():
        raise FileNotFoundError(f"Static results file not found: {static_json_path}")
    
    data = _load_json(static_json_path)
    
    if _parse_analysis_type(data) != "static":
        print("[WARNING] File does not appear to be static analysis results")
    
    print(f"[+] Loaded {len(data.get('functions', {}))} static functions")
    return data


def load_dynamic_results(dynamic_json_path: Path) -> Dict[str, Any]:
    """Load dynamic analysis JSON."""
    print(f"[*] Loading dynamic results from {dynamic_json_path}...")
    
    if not dynamic_json_path.exists():
        raise FileNotFoundError(f"Dynamic results file not found: {dynamic_json_path}")
    
    data = _load_json(dynamic_json_path)
    
    analysis_type = _parse_analysis_type(data)
    if analysis_type != "dynamic":
        print(f"[WARNING] File does not appear to be dynamic analysis results (analysis_type={analysis_type!r})")
    
    print(f"[+] Loaded {len(data.get('functions', {}))} dynamic functions")
    return data


def _parse_address_key(key: Any) -> Optional[int]:
    if isinstance(key, int):
        return key
    if isinstance(key, str):
        s = key.strip()
        try:
            if s.startswith(("0x", "0X")):
                return int(s, 16)
            # allow decimal strings too
            return int(s, 10)
        except ValueError:
            return None
    return None


def _dedupe_ints(values: Iterable[int]) -> List[int]:
    # Preserve stable-ish ordering while deduping.
    seen = set()
    out: List[int] = []
    for v in values:
        if v in seen:
            continue
        seen.add(v)
        out.append(v)
    return out


def _load_function_map(functions_obj: Any, label: str) -> Dict[int, FunctionInfo]:
    out: Dict[int, FunctionInfo] = {}
    if not isinstance(functions_obj, dict):
        print(f"[WARNING] {label} functions missing or invalid type (expected object)")
        return out

    for addr_key, func_data in functions_obj.items():
        addr = _parse_address_key(addr_key)
        if addr is None:
            print(f"[WARNING] Skipping invalid {label} function key: {addr_key!r}")
            continue
        if not isinstance(func_data, dict):
            print(f"[WARNING] Skipping invalid {label} function payload at {addr_key!r}")
            continue
        try:
            func = FunctionInfo.from_dict(func_data)
        except Exception as e:
            print(f"[WARNING] Skipping invalid {label} function {addr_key!r}: {e}")
            continue
        out[addr] = func
    return out


def _load_call_graph_edges(dynamic_data: Dict[str, Any]) -> List[CallGraphEdge]:
    edges_obj = dynamic_data.get("call_graph", {}).get("edges", [])
    if not isinstance(edges_obj, list):
        print("[WARNING] Dynamic call_graph.edges missing or invalid type (expected array)")
        return []

    edges: List[CallGraphEdge] = []
    for edge_data in edges_obj:
        if not isinstance(edge_data, dict):
            continue
        try:
            edges.append(CallGraphEdge.from_dict(edge_data))
        except Exception:
            continue
    return edges


def merge_results(
    static_data: Dict[str, Any], dynamic_data: Dict[str, Any]
) -> Tuple[Dict[int, FunctionInfo], List[CallGraphEdge], Dict[str, Any]]:
    """Merge static and dynamic analysis results into a single function map."""
    print("[*] Merging results...")
    
    static_funcs = _load_function_map(static_data.get("functions", {}), "static")
    dynamic_funcs = _load_function_map(dynamic_data.get("functions", {}), "dynamic")
    call_graph_edges = _load_call_graph_edges(dynamic_data)

    merged: Dict[int, FunctionInfo] = {}

    for addr, func in static_funcs.items():
        merged[addr] = func

    for addr, func in dynamic_funcs.items():
        if addr in merged:
            merged_func = merged[addr]
            merged_func.discovery_method = "both"
            # Dynamic data dominates runtime-relevant fields
            merged_func.call_count = func.call_count
            merged_func.callers = func.callers
            merged_func.callees = func.callees
            if func.signature:
                merged_func.signature = func.signature
        else:
            merged[addr] = func

    # Apply call graph edges to merged map (deduped).
    for edge in call_graph_edges:
        if edge.caller in merged:
            merged[edge.caller].callees = _dedupe_ints([*merged[edge.caller].callees, edge.callee])
        if edge.callee in merged:
            merged[edge.callee].callers = _dedupe_ints([*merged[edge.callee].callers, edge.caller])

    stats = {
        "static_function_count": len(static_funcs),
        "dynamic_function_count": len(dynamic_funcs),
        "merged_function_count": len(merged),
        "call_graph_edge_count": len(call_graph_edges),
    }

    print(f"[+] Merged: {stats['merged_function_count']} total functions, {stats['call_graph_edge_count']} call edges")
    return merged, call_graph_edges, stats


def _confidence_tier(static_present: bool, dynamic_present: bool, call_count: int) -> str:
    # Conservative, deterministic scoring.
    if static_present and dynamic_present and call_count > 0:
        return "high"
    if dynamic_present and call_count > 0 and not static_present:
        return "medium"
    if static_present and not dynamic_present:
        return "medium"
    if dynamic_present and call_count >= 0:
        return "medium"
    return "low"


def load_manual_addresses(manual_path: Path) -> Dict[str, Dict[str, Any]]:
    """
    Load manual mapping file (name -> address).
    Expected format: `scripts/manual_addresses_template.json`
    """
    if not manual_path.exists():
        raise FileNotFoundError(f"Manual address mapping file not found: {manual_path}")

    data = _load_json(manual_path)
    entries = data.get("addresses", [])
    if not isinstance(entries, list):
        raise ValueError("Manual address file invalid: expected 'addresses' array")

    out: Dict[str, Dict[str, Any]] = {}
    for entry in entries:
        if not isinstance(entry, dict):
            continue
        name = entry.get("name")
        addr_str = entry.get("address")
        segment = entry.get("segment", 1)
        if not isinstance(name, str) or not isinstance(addr_str, str):
            continue
        if not addr_str.startswith(("0x", "0X")):
            continue
        try:
            addr = int(addr_str, 16)
        except ValueError:
            continue
        out[name] = {
            "address": addr,
            "segment": int(segment) if isinstance(segment, int) else 1,
            "status": entry.get("status"),
            "evidence": entry.get("evidence"),
        }
    return out


def _default_hashes_hpp_candidates() -> List[Path]:
    # Prefer the repo-local SDK first, then deps copies.
    return [
        Path(__file__).resolve().parents[3] / "RED4ext.SDK" / "include" / "RED4ext" / "Detail" / "AddressHashes.hpp",
        Path(__file__).resolve().parents[2] / "deps" / "red4ext.sdk" / "include" / "RED4ext" / "Detail" / "AddressHashes.hpp",
        Path(__file__).resolve().parents[2] / "src" / "dll" / "Detail" / "AddressHashes.hpp",
    ]


def load_hash_constants(hashes_hpp_path: Optional[Path]) -> Tuple[HashConstants, Path]:
    """
    Load SDK hash constants from AddressHashes.hpp.
    Returns: (name -> u32 hash, resolved_path)
    """
    chosen = hashes_hpp_path
    if chosen is None:
        for candidate in _default_hashes_hpp_candidates():
            if candidate.exists():
                chosen = candidate
                break

    if chosen is None or not chosen.exists():
        checked = ", ".join(str(p) for p in _default_hashes_hpp_candidates())
        raise FileNotFoundError(f"Could not find AddressHashes.hpp (checked: {checked})")

    text = chosen.read_text()
    hashes: HashConstants = {}
    for m in HASH_CONSTANT_PATTERN.finditer(text):
        name = m.group(1)
        value_str = m.group(2)
        value = int(value_str, 16) if value_str.startswith(("0x", "0X")) else int(value_str, 10)
        hashes[name] = value & 0xFFFFFFFF

    if not hashes:
        raise ValueError(f"No hash constants found in {chosen}")

    return hashes, chosen


def _format_sdk_offset(offset: int) -> str:
    # SDK expects: segment:0xHEX (segment is decimal string '1')
    return f"1:0x{offset:X}"


def write_sdk_address_db(
    output_path: Path,
    game_version: str,
    hash_constants: HashConstants,
    manual_addresses: Dict[str, Dict[str, Any]],
) -> Dict[str, Any]:
    """
    Emit `cyberpunk2077_addresses.json` in the exact schema the SDK expects.
    IMPORTANT: avoid putting \"hash\" / \"offset\" keys anywhere except inside Address entries.
    """
    addresses_out: List[Dict[str, str]] = []

    resolved = 0
    unresolved = 0

    for name, hash_value in hash_constants.items():
        manual = manual_addresses.get(name)
        if manual is None:
            addresses_out.append({"hash": str(hash_value), "offset": _format_sdk_offset(0)})
            unresolved += 1
            continue

        if manual.get("segment", 1) != 1:
            # SDK only supports segment 1 currently.
            addresses_out.append({"hash": str(hash_value), "offset": _format_sdk_offset(0)})
            unresolved += 1
            continue

        addr = int(manual["address"])
        offset = addr - IMAGE_BASE
        if offset <= 0:
            addresses_out.append({"hash": str(hash_value), "offset": _format_sdk_offset(0)})
            unresolved += 1
            continue

        addresses_out.append({"hash": str(hash_value), "offset": _format_sdk_offset(offset)})
        resolved += 1

    output = {
        "version": "1.0",
        "game_version": game_version,
        "stats": {
            "total": len(hash_constants),
            "resolved": resolved,
            "unresolved": unresolved,
        },
        "Addresses": addresses_out,
    }

    with open(output_path, "w") as f:
        json.dump(output, f, indent=2, sort_keys=False)

    return {
        "total": len(hash_constants),
        "resolved": resolved,
        "unresolved": unresolved,
    }


def write_report(
    report_path: Path,
    *,
    static_data: Dict[str, Any],
    dynamic_data: Dict[str, Any],
    merge_stats: Dict[str, Any],
    merged_funcs: Dict[int, FunctionInfo],
    hash_constants: HashConstants,
    manual_addresses: Dict[str, Dict[str, Any]],
) -> None:
    static_funcs = _load_function_map(static_data.get("functions", {}), "static")
    dynamic_funcs = _load_function_map(dynamic_data.get("functions", {}), "dynamic")

    per_name: Dict[str, Any] = {}
    confidence_counts = {"high": 0, "medium": 0, "low": 0}

    for name, hash_value in hash_constants.items():
        manual = manual_addresses.get(name)
        addr: Optional[int] = int(manual["address"]) if manual else None
        offset: Optional[int] = (addr - IMAGE_BASE) if addr is not None else None

        static_present = addr in static_funcs if addr is not None else False
        dynamic_present = addr in dynamic_funcs if addr is not None else False

        merged = merged_funcs.get(addr) if addr is not None else None
        call_count = int(merged.call_count) if merged else 0
        prologue_type = merged.prologue_type if merged else None

        confidence = _confidence_tier(static_present, dynamic_present, call_count)
        confidence_counts[confidence] += 1

        per_name[name] = {
            "hash_dec": str(hash_value),
            "manual_address": f"0x{addr:X}" if addr is not None else None,
            "manual_offset": f"0x{offset:X}" if offset is not None and offset >= 0 else None,
            "manual_segment": manual.get("segment") if manual else None,
            "manual_status": manual.get("status") if manual else None,
            "manual_evidence": manual.get("evidence") if manual else None,
            "static_present": static_present,
            "dynamic_present": dynamic_present,
            "call_count": call_count,
            "prologue_type": prologue_type,
            "confidence": confidence,
        }

    needs_attention = [
        name
        for name, info in per_name.items()
        if info["manual_address"] is not None and not (info["static_present"] or info["dynamic_present"])
    ]

    report = {
        "version": "1.0",
        "timestamp": time.strftime("%Y-%m-%d %H:%M:%S"),
        "sources": {
            "static": {
                "binary_path": static_data.get("binary_path", "unknown"),
                "timestamp": static_data.get("timestamp", "unknown"),
            },
            "dynamic": {
                "analysis_type": _parse_analysis_type(dynamic_data),
                "timestamp": dynamic_data.get("timestamp") or dynamic_data.get("metadata", {}).get("timestamp"),
            },
            "manual": {
                "entry_count": len(manual_addresses),
            },
        },
        "merge_stats": merge_stats,
        "sdk_hash_constant_count": len(hash_constants),
        "confidence_counts": confidence_counts,
        "entries": per_name,
        "needs_attention": needs_attention,
    }

    with open(report_path, "w") as f:
        json.dump(report, f, indent=2, sort_keys=False)


def main():
    parser = argparse.ArgumentParser(
        description="Merge Static + Dynamic Analysis Results (macOS pipeline)"
    )
    parser.add_argument(
        "--static",
        type=Path,
        required=True,
        help="Path to static analysis JSON file"
    )
    parser.add_argument(
        "--dynamic",
        type=Path,
        required=True,
        help="Path to dynamic analysis JSON file (v1 or v2 format)"
    )
    parser.add_argument(
        "--manual",
        type=Path,
        default=(Path(__file__).resolve().parents[2] / "scripts" / "manual_addresses_template.json"),
        help="Path to manual address mapping JSON (default: RED4ext/scripts/manual_addresses_template.json)",
    )
    parser.add_argument(
        "--hashes-hpp",
        type=Path,
        default=None,
        help="Path to AddressHashes.hpp (optional; auto-detected by default)",
    )
    parser.add_argument(
        "--game-version",
        type=str,
        default="unknown",
        help="Game version string to embed in output (default: unknown)",
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=Path("cyberpunk2077_addresses.json"),
        help="Output SDK address DB JSON (default: cyberpunk2077_addresses.json)",
    )
    parser.add_argument(
        "--report",
        type=Path,
        default=None,
        help="Optional evidence report JSON path (recommended)",
    )
    
    args = parser.parse_args()
    
    print("=" * 70)
    print("Merge Results - Combine Static + Dynamic Analysis")
    print("=" * 70)
    print(f"Static: {args.static}")
    print(f"Dynamic: {args.dynamic}")
    print(f"Manual: {args.manual}")
    if args.hashes_hpp:
        print(f"Hashes: {args.hashes_hpp}")
    print(f"Game Version: {args.game_version}")
    print(f"Output: {args.output}")
    if args.report:
        print(f"Report: {args.report}")
    print()
    
    try:
        # Load results
        static_data = load_static_results(args.static)
        dynamic_data = load_dynamic_results(args.dynamic)
        
        # Merge results
        merged_funcs, _call_graph_edges, merge_stats = merge_results(static_data, dynamic_data)

        # Load manual + hash constants
        manual_addresses = load_manual_addresses(args.manual)
        hash_constants, hashes_path = load_hash_constants(args.hashes_hpp)
        print(f"[+] Loaded {len(hash_constants)} hash constants from {hashes_path}")
        print(f"[+] Loaded {len(manual_addresses)} manual name->address mappings")

        # Emit SDK DB
        sdk_stats = write_sdk_address_db(
            args.output, args.game_version, hash_constants, manual_addresses
        )

        print("[+] Wrote SDK address DB")
        print(f"[+]   - total: {sdk_stats['total']}")
        print(f"[+]   - resolved: {sdk_stats['resolved']}")
        print(f"[+]   - unresolved: {sdk_stats['unresolved']}")

        # Optional report (separate file)
        if args.report:
            write_report(
                args.report,
                static_data=static_data,
                dynamic_data=dynamic_data,
                merge_stats=merge_stats,
                merged_funcs=merged_funcs,
                hash_constants=hash_constants,
                manual_addresses=manual_addresses,
            )
            print("[+] Wrote evidence report")
        
        print()
        print("=" * 70)
        print("Merge complete!")
        print(f"Results saved to: {args.output}")
        print("=" * 70)
        
    except FileNotFoundError as e:
        print(f"[ERROR] {e}")
        sys.exit(1)
    except json.JSONDecodeError as e:
        print(f"[ERROR] Invalid JSON format: {e}")
        sys.exit(1)
    except Exception as e:
        print(f"[ERROR] Unexpected error: {e}")
        import traceback
        traceback.print_exc()
        sys.exit(1)


if __name__ == "__main__":
    main()
