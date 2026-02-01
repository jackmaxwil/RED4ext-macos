#!/usr/bin/env python3
"""
Static Analysis Script - Function Discovery via otool

Discovers functions, strings, and segments using static analysis (otool).
Does not require the game to be running.

Usage:
    python3 static_analysis.py \
      --binary "/path/to/Cyberpunk2077" \
      --output static_functions.json
"""

import subprocess
import re
import json
import argparse
import sys
import time
from pathlib import Path
from typing import Dict, List, Tuple, Optional

# Import shared data structures
try:
    from re_common import FunctionInfo, IMAGE_BASE, TEXT_SEGMENT
except ImportError:
    # Fallback if running standalone
    sys.path.insert(0, str(Path(__file__).parent))
    from re_common import FunctionInfo, IMAGE_BASE, TEXT_SEGMENT

# Try to import existing address discovery utilities
try:
    import sys
    sys.path.insert(0, str(Path(__file__).parent.parent))
    from lib.address_discovery import AddressDiscovery
except ImportError:
    AddressDiscovery = None

# ARM64 prologue patterns
ARM64_STP_PATTERN = re.compile(r'stp\s+x29,\s+x30,\s+\[sp,\s*#-?0x?[0-9a-f]+\]!', re.IGNORECASE)
ARM64_SUB_SP_PATTERN = re.compile(r'sub\s+sp,\s+sp,\s+#0x?[0-9a-f]+', re.IGNORECASE)


def parse_segments(binary_path: Path) -> Dict[str, Dict[str, str]]:
    """Parse Mach-O segments using otool."""
    print("[*] Parsing Mach-O segments...")
    segments = {}
    
    try:
        result = subprocess.run(
            ['otool', '-l', str(binary_path)],
            capture_output=True,
            text=True,
            timeout=30,
            check=True
        )
    except (subprocess.CalledProcessError, FileNotFoundError) as e:
        print(f"[ERROR] Failed to parse segments: {e}")
        if isinstance(e, FileNotFoundError):
            print("[ERROR] otool not found. Install Xcode Command Line Tools:")
            print("        xcode-select --install")
        return segments
    except subprocess.TimeoutExpired:
        print("[ERROR] otool timed out parsing segments")
        return segments
    
    current_seg = None
    vmaddr = None
    vmsize = None
    
    for line in result.stdout.split('\n'):
        line = line.strip()
        
        if 'segname' in line:
            # Save previous segment if exists
            if current_seg and vmaddr is not None:
                segments[current_seg] = {
                    "vmaddr": f"0x{vmaddr:X}",
                    "vmsize": f"0x{vmsize:X}" if vmsize is not None else "0x0"
                }
            
            # Extract segment name
            parts = line.split()
            if len(parts) >= 2:
                current_seg = parts[1]
                vmaddr = None
                vmsize = None
        
        elif 'vmaddr' in line and current_seg:
            parts = line.split()
            if len(parts) >= 2:
                try:
                    vmaddr = int(parts[1], 16)
                except ValueError:
                    pass
        
        elif 'vmsize' in line and current_seg:
            parts = line.split()
            if len(parts) >= 2:
                try:
                    vmsize = int(parts[1], 16)
                except ValueError:
                    pass
    
    # Save last segment
    if current_seg and vmaddr is not None:
        segments[current_seg] = {
            "vmaddr": f"0x{vmaddr:X}",
            "vmsize": f"0x{vmsize:X}" if vmsize is not None else "0x0"
        }
    
    print(f"[+] Parsed {len(segments)} segments")
    return segments


def extract_strings(binary_path: Path) -> Dict[str, str]:
    """Extract strings from __TEXT __cstring section."""
    print("[*] Extracting strings...")
    strings = {}
    
    try:
        result = subprocess.run(
            ['otool', '-s', '__TEXT', '__cstring', str(binary_path)],
            capture_output=True,
            text=True,
            timeout=60,
            check=True
        )
    except (subprocess.CalledProcessError, FileNotFoundError) as e:
        print(f"[WARNING] Failed to extract strings: {e}")
        return strings
    except subprocess.TimeoutExpired:
        print("[WARNING] String extraction timed out")
        return strings
    
    # Parse otool string output
    # Format: "0000000106c148b8\t52202121 72756365 65766973 73736120"
    # Address is tab-separated from hex bytes (groups of 8 hex digits = 4 bytes, little-endian)
    current_addr = None
    current_str = ""
    string_count = 0
    
    for line in result.stdout.split('\n'):
        # Skip header lines
        if 'Contents of' in line or not line.strip() or line.startswith('/'):
            if current_str and current_addr:
                strings[current_str] = f"0x{current_addr:X}"
                string_count += 1
                if string_count % 1000 == 0:
                    print(f"  Extracted {string_count} strings...")
            current_str = ""
            current_addr = None
            continue
        
        # Parse address and hex (tab-separated)
        if '\t' in line:
            parts = line.split('\t', 1)
            if len(parts) == 2:
                try:
                    addr_str = parts[0].strip()
                    # Address is 16 hex digits
                    if len(addr_str) == 16:
                        addr = int(addr_str, 16)
                        hex_data = parts[1].strip()
                        
                        # Convert hex groups to bytes
                        # Each group is 8 hex digits (4 bytes) in little-endian
                        hex_groups = hex_data.split()
                        for group in hex_groups:
                            if len(group) == 8:  # 4 bytes
                                # Convert little-endian 4-byte group to bytes
                                # Group "52202121" = bytes [0x21, 0x21, 0x20, 0x52] in little-endian
                                try:
                                    # Parse as 32-bit little-endian integer
                                    group_int = int(group, 16)
                                    # Extract bytes in reverse order (little-endian)
                                    bytes_list = [
                                        (group_int >> 0) & 0xFF,
                                        (group_int >> 8) & 0xFF,
                                        (group_int >> 16) & 0xFF,
                                        (group_int >> 24) & 0xFF
                                    ]
                                    
                                    for byte_val in bytes_list:
                                        if byte_val == 0:
                                            # Null terminator - save string
                                            if current_str and current_addr:
                                                strings[current_str] = f"0x{current_addr:X}"
                                                string_count += 1
                                                if string_count % 1000 == 0:
                                                    print(f"  Extracted {string_count} strings...")
                                            current_str = ""
                                            current_addr = None
                                        elif 32 <= byte_val <= 126:  # Printable ASCII
                                            if current_addr is None:
                                                current_addr = addr
                                            current_str += chr(byte_val)
                                        else:
                                            # Non-printable - end current string
                                            if current_str and current_addr:
                                                strings[current_str] = f"0x{current_addr:X}"
                                                string_count += 1
                                                if string_count % 1000 == 0:
                                                    print(f"  Extracted {string_count} strings...")
                                            current_str = ""
                                            current_addr = None
                                except ValueError:
                                    pass
                except ValueError:
                    pass
    
    # Final string
    if current_str and current_addr:
        strings[current_str] = f"0x{current_addr:X}"
        string_count += 1
    
    print(f"[+] Extracted {len(strings)} strings")
    return strings


def discover_functions_static(binary_path: Path) -> Dict[int, FunctionInfo]:
    """Discover functions via static analysis using otool."""
    print("[*] Discovering functions via static analysis...")
    
    functions = {}
    
    # Use existing AddressDiscovery if available
    if AddressDiscovery:
        try:
            disco = AddressDiscovery(str(binary_path))
            print("[*] Using AddressDiscovery utilities...")
            # Note: AddressDiscovery has its own methods, but we'll use otool directly
            # for consistency with the plan
        except Exception as e:
            print(f"[WARNING] AddressDiscovery failed: {e}")
    
    # Parse disassembly to find function prologues
    try:
        result = subprocess.run(
            ['otool', '-tV', str(binary_path)],
            capture_output=True,
            text=True,
            timeout=300,  # 5 minutes for large binaries
            check=True
        )
    except (subprocess.CalledProcessError, FileNotFoundError) as e:
        print(f"[ERROR] Failed to disassemble: {e}")
        return functions
    except subprocess.TimeoutExpired:
        print("[ERROR] Disassembly timed out")
        return functions
    
    # Parse otool disassembly output
    # Format: "00000001000021a8\tstp\tx22, x21, [sp, #-0x30]!"
    func_count = 0
    
    for line in result.stdout.split('\n'):
        # Skip section headers
        if line.startswith('(') or not line.strip():
            continue
        
        # Check for address line: "address\tinstruction"
        if '\t' in line:
            parts = line.split('\t', 1)
            if len(parts) == 2:
                try:
                    addr_str = parts[0].strip()
                    # Skip if not a hex address
                    if not addr_str.startswith('0x') and len(addr_str) == 16:
                        # Format: 00000001000021a8 (16 hex digits)
                        addr = int(addr_str, 16)
                    elif addr_str.startswith('0x'):
                        addr = int(addr_str, 16)
                    else:
                        continue
                    
                    instruction = parts[1].strip()
                    
                    # Check for function prologue patterns
                    prologue_type = None
                    instruction_lower = instruction.lower()
                    
                    # STP X29, X30 pattern (common function prologue)
                    if 'stp' in instruction_lower and 'x29' in instruction_lower and 'x30' in instruction_lower:
                        prologue_type = "STP_X29_X30"
                    # SUB SP, SP pattern (stack allocation)
                    elif 'sub' in instruction_lower and 'sp' in instruction_lower and instruction_lower.count('sp') >= 2:
                        prologue_type = "SUB_SP"
                    
                    if prologue_type:
                        offset = addr - IMAGE_BASE
                        if offset > 0 and offset < 0x10000000:  # Reasonable range
                            func_addr = IMAGE_BASE + offset
                            if func_addr not in functions:
                                functions[func_addr] = FunctionInfo(
                                    offset=offset,
                                    address=func_addr,
                                    confidence="low",
                                    discovery_method="static",
                                    prologue_type=prologue_type
                                )
                                func_count += 1
                                if func_count % 100 == 0:
                                    print(f"  Found {func_count} functions...")
                except (ValueError, IndexError):
                    continue
    
    print(f"[+] Static discovery: Found {len(functions)} functions")
    return functions


def export_static_results(output_path: Path, functions: Dict[int, FunctionInfo],
                         strings: Dict[str, str], segments: Dict[str, Dict[str, str]],
                         binary_path: Optional[Path] = None) -> None:
    """Export static analysis results to JSON."""
    print(f"[*] Exporting results to {output_path}...")
    
    # Convert functions to dict format
    functions_dict = {}
    for addr, func in functions.items():
        functions_dict[f"0x{addr:X}"] = func.to_dict()
    
    # Build output structure
    output = {
        "version": "1.0",
        "analysis_type": "static",
        "binary_path": str(binary_path) if binary_path else "unknown",
        "timestamp": time.strftime("%Y-%m-%d %H:%M:%S"),
        "functions": functions_dict,
        "strings": strings,
        "segments": segments,
        "stats": {
            "total_functions": len(functions),
            "total_strings": len(strings),
            "total_segments": len(segments)
        }
    }
    
    # Write JSON
    with open(output_path, 'w') as f:
        json.dump(output, f, indent=2)
    
    print(f"[+] Exported {len(functions)} functions, {len(strings)} strings, {len(segments)} segments")


def main():
    parser = argparse.ArgumentParser(
        description="Static Analysis - Discover functions via otool"
    )
    parser.add_argument(
        '--binary',
        type=Path,
        required=True,
        help='Path to Cyberpunk2077 binary'
    )
    parser.add_argument(
        '--output',
        type=Path,
        default=Path('static_functions.json'),
        help='Output JSON file (default: static_functions.json)'
    )
    
    args = parser.parse_args()
    
    # Validate binary path
    if not args.binary.exists():
        print(f"[ERROR] Binary not found: {args.binary}")
        sys.exit(1)
    
    print("=" * 70)
    print("Static Analysis - Function Discovery")
    print("=" * 70)
    print(f"Binary: {args.binary}")
    print(f"Output: {args.output}")
    print()
    
    # Parse segments
    segments = parse_segments(args.binary)
    
    # Extract strings
    strings = extract_strings(args.binary)
    
    # Discover functions
    functions = discover_functions_static(args.binary)
    
    # Export results
    export_static_results(args.output, functions, strings, segments)
    
    # Update binary_path in output
    with open(args.output, 'r') as f:
        data = json.load(f)
    data["binary_path"] = str(args.binary)
    with open(args.output, 'w') as f:
        json.dump(data, f, indent=2)
    
    print()
    print("=" * 70)
    print("Static analysis complete!")
    print(f"Results saved to: {args.output}")
    print("=" * 70)


if __name__ == "__main__":
    main()
