#!/usr/bin/env python3
"""
Cyberpunk 2077 Address Discovery Library

Codified techniques for finding function addresses in the macOS ARM64 binary.
Based on reverse engineering work done for RED4ext/TweakXL macOS port.

Usage:
    from lib.address_discovery import AddressDiscovery
    
    disco = AddressDiscovery("/path/to/Cyberpunk2077")
    
    # Find by string reference
    addr = disco.find_by_string(".tweak")
    
    # Find by member access pattern
    addrs = disco.find_by_offset_access(0xD8)
    
    # Validate function prologue
    if disco.is_valid_function(0x2B79AC0):
        print("Valid function at offset")
"""

import subprocess
import struct
import re
from pathlib import Path
from typing import Optional, List, Tuple, Dict, Set
from dataclasses import dataclass
from functools import lru_cache


# Constants
IMAGE_BASE = 0x100000000
TEXT_SEGMENT = 1

# ARM64 instruction patterns (little-endian)
ARM64_STP_X29_X30 = 0xA9  # STP X29, X30, [SP, #imm]! (varies by immediate)
ARM64_SUB_SP = 0xD1  # SUB SP, SP, #imm


@dataclass
class FunctionInfo:
    """Information about a discovered function."""
    offset: int
    address: int
    name: Optional[str] = None
    confidence: float = 0.0
    evidence: List[str] = None
    
    def __post_init__(self):
        if self.evidence is None:
            self.evidence = []
    
    @property
    def json_offset(self) -> str:
        """Format offset for cyberpunk2077_addresses.json"""
        return f"{TEXT_SEGMENT}:0x{self.offset:X}"


@dataclass
class StringRef:
    """A string reference in the binary."""
    string: str
    string_addr: int
    ref_addr: int
    function_addr: Optional[int] = None


class AddressDiscovery:
    """
    Address discovery toolkit for Cyberpunk 2077 macOS binary.
    
    Implements the most effective techniques found during RE work:
    1. String reference tracing (~85% success rate)
    2. Member offset access patterns
    3. Function proximity search
    4. Prologue validation
    """
    
    def __init__(self, binary_path: str):
        self.binary_path = Path(binary_path)
        if not self.binary_path.exists():
            raise FileNotFoundError(f"Binary not found: {binary_path}")
        
        self._binary_data: Optional[bytes] = None
        self._symbols: Optional[Dict[str, int]] = None
        self._strings: Optional[Dict[str, int]] = None
    
    @property
    def binary_data(self) -> bytes:
        """Lazy-load binary data."""
        if self._binary_data is None:
            self._binary_data = self.binary_path.read_bytes()
        return self._binary_data
    
    # =========================================================================
    # Technique 1: String Reference Tracing (Most Effective)
    # =========================================================================
    
    def find_by_string(
        self, 
        search_string: str,
        max_distance: int = 0x1000
    ) -> Optional[FunctionInfo]:
        """
        Find function by tracing string reference.
        
        This is the most effective technique (~85% success rate).
        
        Algorithm:
        1. Find string in __cstring section
        2. Find ADRP+ADD that references string address
        3. Walk backwards to function prologue
        
        Args:
            search_string: String to search for
            max_distance: Max bytes to walk back for prologue
            
        Returns:
            FunctionInfo if found, None otherwise
        """
        # Step 1: Find string address
        string_addr = self._find_string_address(search_string)
        if string_addr is None:
            return None
        
        # Step 2: Find ADRP+ADD referencing string
        ref_addr = self._find_adrp_add_for_address(string_addr)
        if ref_addr is None:
            return None
        
        # Step 3: Walk back to function prologue
        func_offset = self._find_function_prologue(ref_addr - IMAGE_BASE, max_distance)
        if func_offset is None:
            return None
        
        return FunctionInfo(
            offset=func_offset,
            address=IMAGE_BASE + func_offset,
            confidence=0.85,
            evidence=[
                f"String reference: '{search_string}'",
                f"String at: 0x{string_addr:X}",
                f"Reference at: 0x{ref_addr:X}"
            ]
        )
    
    def find_all_string_refs(self, search_string: str) -> List[StringRef]:
        """Find all references to a string."""
        results = []
        string_addr = self._find_string_address(search_string)
        if string_addr is None:
            return results
        
        # Find all ADRP+ADD patterns referencing this address
        refs = self._find_all_adrp_add_for_address(string_addr)
        for ref_addr in refs:
            func_offset = self._find_function_prologue(ref_addr - IMAGE_BASE, 0x1000)
            results.append(StringRef(
                string=search_string,
                string_addr=string_addr,
                ref_addr=ref_addr,
                function_addr=IMAGE_BASE + func_offset if func_offset else None
            ))
        
        return results
    
    # =========================================================================
    # Technique 2: Member Offset Access Patterns
    # =========================================================================
    
    def find_by_offset_access(
        self,
        offset: int,
        base_reg: str = "X0"
    ) -> List[FunctionInfo]:
        """
        Find functions that access a specific struct member offset.
        
        Useful for finding class methods that access known member variables.
        
        Example:
            # Find functions accessing StatsDataSystem.statRecords at 0xD8
            funcs = disco.find_by_offset_access(0xD8)
        
        Args:
            offset: Member offset to search for
            base_reg: Expected base register (usually X0 for 'this')
            
        Returns:
            List of FunctionInfo for functions accessing this offset
        """
        results = []
        
        # Pattern: LDR Xn, [base_reg, #offset]
        # We search disassembly output for this pattern
        try:
            output = subprocess.check_output(
                ["otool", "-tV", str(self.binary_path)],
                text=True,
                stderr=subprocess.DEVNULL
            )
        except subprocess.CalledProcessError:
            return results
        
        # Look for offset access pattern
        pattern = rf"ldr\s+[xw]\d+,\s*\[{base_reg.lower()},\s*#0x{offset:x}\]"
        
        seen_funcs: Set[int] = set()
        for match in re.finditer(pattern, output, re.IGNORECASE):
            # Find the address of this instruction
            # otool format: "address:\t<disasm>"
            line_start = output.rfind('\n', 0, match.start()) + 1
            addr_match = re.match(r'([0-9a-f]+):', output[line_start:line_start+20])
            if addr_match:
                instr_addr = int(addr_match.group(1), 16)
                func_offset = self._find_function_prologue(
                    instr_addr - IMAGE_BASE, 0x1000
                )
                if func_offset and func_offset not in seen_funcs:
                    seen_funcs.add(func_offset)
                    results.append(FunctionInfo(
                        offset=func_offset,
                        address=IMAGE_BASE + func_offset,
                        confidence=0.7,
                        evidence=[f"Accesses offset 0x{offset:X} from {base_reg}"]
                    ))
        
        return results
    
    # =========================================================================
    # Technique 3: Function Proximity Search
    # =========================================================================
    
    def find_nearby_functions(
        self,
        known_offset: int,
        search_range: int = 0x10000,
        direction: str = "both"
    ) -> List[FunctionInfo]:
        """
        Find functions near a known function.
        
        Related functions tend to cluster together in memory.
        
        Args:
            known_offset: Offset of known function
            search_range: Bytes to search in each direction
            direction: "before", "after", or "both"
            
        Returns:
            List of FunctionInfo for nearby functions
        """
        results = []
        
        if direction in ("before", "both"):
            start = max(0, known_offset - search_range)
            results.extend(self._scan_for_prologues(start, known_offset))
        
        if direction in ("after", "both"):
            end = min(len(self.binary_data), known_offset + search_range)
            results.extend(self._scan_for_prologues(known_offset + 4, end))
        
        return results
    
    # =========================================================================
    # Technique 4: Prologue Validation
    # =========================================================================
    
    def is_valid_function(self, offset: int) -> bool:
        """
        Check if offset points to a valid function prologue.
        
        Valid ARM64 function prologues:
        - STP X29, X30, [SP, #-imm]!  (frame pointer setup)
        - SUB SP, SP, #imm            (stack allocation)
        """
        if offset < 0 or offset + 4 > len(self.binary_data):
            return False
        
        instr = struct.unpack_from('<I', self.binary_data, offset)[0]
        
        # Check for STP X29, X30, [SP, #-imm]!
        # Encoding: 1010 1001 0xxx xxxx xxxx xx11 1110 1101
        if (instr & 0xFFE07FFF) == 0xA9007BFD:
            return True
        
        # Check for SUB SP, SP, #imm
        # Encoding: 1101 0001 00xx xxxx xxxx xxxx xx11 1111
        if (instr & 0xFFC003FF) == 0xD10003FF:
            return True
        
        return False
    
    def validate_and_get_info(self, offset: int, name: str = None) -> Optional[FunctionInfo]:
        """Validate offset and return FunctionInfo if valid."""
        if self.is_valid_function(offset):
            return FunctionInfo(
                offset=offset,
                address=IMAGE_BASE + offset,
                name=name,
                confidence=1.0 if name else 0.5,
                evidence=["Valid function prologue"]
            )
        return None
    
    # =========================================================================
    # Utility Methods
    # =========================================================================
    
    @lru_cache(maxsize=1000)
    def _find_string_address(self, search_string: str) -> Optional[int]:
        """Find address of string in __cstring section."""
        # Search in binary data
        encoded = search_string.encode('utf-8') + b'\x00'
        pos = self.binary_data.find(encoded)
        if pos != -1:
            return IMAGE_BASE + pos
        return None
    
    def _find_adrp_add_for_address(self, target_addr: int) -> Optional[int]:
        """Find ADRP+ADD instruction pair referencing target address."""
        refs = self._find_all_adrp_add_for_address(target_addr)
        return refs[0] if refs else None
    
    def _find_all_adrp_add_for_address(self, target_addr: int) -> List[int]:
        """Find all ADRP+ADD pairs referencing target address."""
        results = []
        target_page = target_addr & ~0xFFF
        target_offset = target_addr & 0xFFF
        
        # Scan for ADRP instructions
        for offset in range(0, len(self.binary_data) - 8, 4):
            instr = struct.unpack_from('<I', self.binary_data, offset)[0]
            
            # Check if ADRP (1xx1 0000 xxxx xxxx xxxx xxxx xxxx xxxx)
            if (instr & 0x9F000000) != 0x90000000:
                continue
            
            # Decode ADRP
            rd = instr & 0x1F
            immhi = (instr >> 5) & 0x7FFFF
            immlo = (instr >> 29) & 0x3
            imm = (immhi << 2) | immlo
            if imm & 0x100000:  # Sign extend
                imm |= ~0x1FFFFF
            
            pc = IMAGE_BASE + offset
            page_addr = (pc & ~0xFFF) + (imm << 12)
            
            if page_addr != target_page:
                continue
            
            # Check next instruction for ADD with target offset
            next_instr = struct.unpack_from('<I', self.binary_data, offset + 4)[0]
            
            # ADD Xd, Xn, #imm (1001 0001 00ii iiii iiii iinn nnnd dddd)
            if (next_instr & 0xFFC00000) == 0x91000000:
                add_rd = next_instr & 0x1F
                add_rn = (next_instr >> 5) & 0x1F
                add_imm = (next_instr >> 10) & 0xFFF
                
                if add_rn == rd and add_imm == target_offset:
                    results.append(IMAGE_BASE + offset)
        
        return results
    
    def _find_function_prologue(
        self, 
        start_offset: int, 
        max_distance: int
    ) -> Optional[int]:
        """Walk backwards from offset to find function prologue."""
        # Align to 4-byte boundary
        offset = start_offset & ~3
        
        # Search backwards for prologue
        search_start = max(0, offset - max_distance)
        
        for check_offset in range(offset, search_start, -4):
            if self.is_valid_function(check_offset):
                return check_offset
        
        return None
    
    def _scan_for_prologues(
        self, 
        start: int, 
        end: int
    ) -> List[FunctionInfo]:
        """Scan range for function prologues."""
        results = []
        offset = start & ~3  # Align
        
        while offset < end:
            if self.is_valid_function(offset):
                results.append(FunctionInfo(
                    offset=offset,
                    address=IMAGE_BASE + offset,
                    confidence=0.3,
                    evidence=["Prologue scan"]
                ))
                offset += 16  # Skip ahead (functions are usually >16 bytes)
            else:
                offset += 4
        
        return results
    
    # =========================================================================
    # High-Level Discovery Methods
    # =========================================================================
    
    def discover_tweakdb_functions(self) -> Dict[str, FunctionInfo]:
        """
        Discover TweakDB-related functions using known patterns.
        
        Returns:
            Dict mapping function names to FunctionInfo
        """
        results = {}
        
        # Known string markers for TweakDB functions
        markers = {
            "TweakDB_TryLoad": ".tweak",
            "TweakDB_Load": "LoadOptimized",
            "TweakDB_Init": "TweakDB",
        }
        
        for func_name, marker in markers.items():
            info = self.find_by_string(marker)
            if info:
                info.name = func_name
                results[func_name] = info
        
        # Use proximity to find related functions
        if results:
            known_offset = list(results.values())[0].offset
            nearby = self.find_nearby_functions(known_offset, 0x8000)
            for i, info in enumerate(nearby):
                if info.offset not in {r.offset for r in results.values()}:
                    info.name = f"TweakDB_Unknown_{i}"
                    results[info.name] = info
        
        return results
    
    def discover_stats_functions(self) -> Dict[str, FunctionInfo]:
        """
        Discover StatsDataSystem functions by member access patterns.
        
        Returns:
            Dict mapping function names to FunctionInfo
        """
        results = {}
        
        # StatsDataSystem member offsets
        offsets = {
            0xD8: "statRecords",
            0xE8: "statParams", 
            0xFC: "statLock"
        }
        
        for offset, member in offsets.items():
            funcs = self.find_by_offset_access(offset)
            for i, info in enumerate(funcs):
                name = f"Stats_{member}_{i}"
                info.name = name
                results[name] = info
        
        return results


# =============================================================================
# CLI Interface
# =============================================================================

def main():
    """Command-line interface for address discovery."""
    import argparse
    import json
    
    parser = argparse.ArgumentParser(
        description="Discover function addresses in Cyberpunk 2077 binary"
    )
    parser.add_argument("binary", help="Path to Cyberpunk2077 binary")
    parser.add_argument("--string", "-s", help="Find by string reference")
    parser.add_argument("--offset", "-o", type=lambda x: int(x, 0),
                       help="Find by member offset access (hex)")
    parser.add_argument("--near", "-n", type=lambda x: int(x, 0),
                       help="Find functions near offset (hex)")
    parser.add_argument("--validate", "-v", type=lambda x: int(x, 0),
                       help="Validate offset as function (hex)")
    parser.add_argument("--discover", "-d", choices=["tweakdb", "stats"],
                       help="Auto-discover function set")
    parser.add_argument("--json", "-j", action="store_true",
                       help="Output as JSON")
    
    args = parser.parse_args()
    
    disco = AddressDiscovery(args.binary)
    results = []
    
    if args.string:
        info = disco.find_by_string(args.string)
        if info:
            results.append(info)
    
    if args.offset:
        results.extend(disco.find_by_offset_access(args.offset))
    
    if args.near:
        results.extend(disco.find_nearby_functions(args.near))
    
    if args.validate is not None:
        info = disco.validate_and_get_info(args.validate)
        if info:
            results.append(info)
    
    if args.discover == "tweakdb":
        results.extend(disco.discover_tweakdb_functions().values())
    elif args.discover == "stats":
        results.extend(disco.discover_stats_functions().values())
    
    # Output
    if args.json:
        output = [
            {
                "name": r.name,
                "offset": f"0x{r.offset:X}",
                "address": f"0x{r.address:X}",
                "json_offset": r.json_offset,
                "confidence": r.confidence,
                "evidence": r.evidence
            }
            for r in results
        ]
        print(json.dumps(output, indent=2))
    else:
        for r in results:
            print(f"\n{'=' * 60}")
            print(f"Name: {r.name or 'Unknown'}")
            print(f"Offset: 0x{r.offset:X}")
            print(f"Address: 0x{r.address:X}")
            print(f"JSON: {r.json_offset}")
            print(f"Confidence: {r.confidence:.0%}")
            if r.evidence:
                print("Evidence:")
                for e in r.evidence:
                    print(f"  - {e}")


if __name__ == "__main__":
    main()
