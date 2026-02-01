#!/usr/bin/env python3
"""
Function Call Graph Tracer

Traces function calls from a given starting address to build a call graph.
Useful for understanding RT/PT execution flow.

Usage:
    python3 call_graph_tracer.py /path/to/Cyberpunk2077 --start 0x2B79AC0
    python3 call_graph_tracer.py /path/to/Cyberpunk2077 --symbol "CRenderNode_RenderRayTracedRTXDI"
"""

import subprocess
import struct
import re
from pathlib import Path
from dataclasses import dataclass, field
from typing import List, Dict, Optional, Set, Tuple
from collections import defaultdict
import argparse
import json

IMAGE_BASE = 0x100000000


@dataclass
class FunctionCall:
    """A function call relationship."""
    caller_addr: int
    callee_addr: int
    caller_offset: int
    callee_offset: int
    call_site_offset: int  # Where in caller the call happens
    callee_name: Optional[str] = None


@dataclass
class FunctionNode:
    """A function in the call graph."""
    address: int
    offset: int
    name: Optional[str] = None
    calls: List[int] = field(default_factory=list)  # Addresses this function calls
    called_by: List[int] = field(default_factory=list)  # Addresses that call this
    size: int = 0


class CallGraphTracer:
    """Traces function call graphs in ARM64 binary."""
    
    def __init__(self, binary_path: str):
        self.binary_path = Path(binary_path)
        if not self.binary_path.exists():
            raise FileNotFoundError(f"Binary not found: {binary_path}")
        
        self.binary_data: bytes = b''
        self.symbols: Dict[int, str] = {}  # addr -> name
        self.functions: Dict[int, FunctionNode] = {}
        self.calls: List[FunctionCall] = []
        
    def load(self):
        """Load binary and symbols."""
        print("[*] Loading binary...")
        self.binary_data = self.binary_path.read_bytes()
        
        print("[*] Loading symbols...")
        self._load_symbols()
    
    def _load_symbols(self):
        """Load symbols via nm."""
        try:
            result = subprocess.run(
                ['nm', '-n', str(self.binary_path)],
                capture_output=True,
                text=True,
                timeout=120
            )
            
            # Demangle in batch
            demangled = subprocess.run(
                ['c++filt'],
                input=result.stdout,
                capture_output=True,
                text=True,
                timeout=60
            )
            
            for line in demangled.stdout.split('\n'):
                parts = line.split()
                if len(parts) >= 3 and parts[1] in ('T', 't', 'S', 's'):
                    try:
                        addr = int(parts[0], 16)
                        name = ' '.join(parts[2:])
                        self.symbols[addr] = name
                    except ValueError:
                        continue
                        
        except (subprocess.TimeoutExpired, Exception) as e:
            print(f"[!] Symbol loading error: {e}")
    
    def find_symbol_address(self, pattern: str) -> Optional[int]:
        """Find address of symbol matching pattern."""
        pattern_lower = pattern.lower()
        for addr, name in self.symbols.items():
            if pattern_lower in name.lower():
                return addr
        return None
    
    def trace_calls(self, start_offset: int, max_depth: int = 3, 
                   max_instructions: int = 500) -> Dict[int, FunctionNode]:
        """
        Trace function calls from a starting address.
        
        Args:
            start_offset: Starting offset (relative to image base)
            max_depth: Maximum call depth to trace
            max_instructions: Max instructions to scan per function
        """
        start_addr = IMAGE_BASE + start_offset
        
        print(f"[*] Tracing from 0x{start_offset:X} (depth={max_depth})...")
        
        visited: Set[int] = set()
        to_visit: List[Tuple[int, int]] = [(start_addr, 0)]  # (addr, depth)
        
        while to_visit:
            addr, depth = to_visit.pop(0)
            
            if addr in visited or depth > max_depth:
                continue
            
            visited.add(addr)
            offset = addr - IMAGE_BASE
            
            # Create function node
            name = self.symbols.get(addr)
            node = FunctionNode(
                address=addr,
                offset=offset,
                name=name
            )
            self.functions[addr] = node
            
            # Find calls from this function
            calls = self._scan_for_calls(offset, max_instructions)
            
            for call_offset, target_addr in calls:
                node.calls.append(target_addr)
                
                # Record call relationship
                self.calls.append(FunctionCall(
                    caller_addr=addr,
                    callee_addr=target_addr,
                    caller_offset=offset,
                    callee_offset=target_addr - IMAGE_BASE,
                    call_site_offset=call_offset,
                    callee_name=self.symbols.get(target_addr)
                ))
                
                # Add to visit queue
                if target_addr not in visited:
                    to_visit.append((target_addr, depth + 1))
        
        # Build called_by lists
        for call in self.calls:
            if call.callee_addr in self.functions:
                self.functions[call.callee_addr].called_by.append(call.caller_addr)
        
        print(f"[+] Traced {len(self.functions)} functions, {len(self.calls)} calls")
        return self.functions
    
    def _scan_for_calls(self, start_offset: int, max_instructions: int) -> List[Tuple[int, int]]:
        """
        Scan for BL (branch with link) instructions.
        
        Returns list of (call_site_offset, target_address)
        """
        calls = []
        
        # Ensure alignment
        offset = start_offset & ~3
        
        for i in range(max_instructions):
            if offset + 4 > len(self.binary_data):
                break
            
            instr = struct.unpack_from('<I', self.binary_data, offset)[0]
            
            # Check for BL instruction (1001 01xx xxxx xxxx xxxx xxxx xxxx xxxx)
            if (instr & 0xFC000000) == 0x94000000:
                # Extract signed 26-bit immediate
                imm26 = instr & 0x03FFFFFF
                if imm26 & 0x02000000:  # Sign extend
                    imm26 |= ~0x03FFFFFF
                    imm26 = imm26 & 0xFFFFFFFF
                    imm26 = -(0x100000000 - imm26)
                
                target = IMAGE_BASE + offset + (imm26 * 4)
                
                # Filter out obviously invalid targets
                if IMAGE_BASE <= target < IMAGE_BASE + len(self.binary_data):
                    calls.append((offset, target))
            
            # Check for RET (end of function)
            if instr == 0xD65F03C0:
                break
            
            offset += 4
        
        return calls
    
    def get_rt_subgraph(self) -> Dict[int, FunctionNode]:
        """Get subgraph of RT-related functions."""
        rt_keywords = ['raytrac', 'rtxdi', 'restir', 'denois', 'shadow', 
                      'reflect', 'gi', 'accel', 'bvh', 'nrd']
        
        rt_funcs = {}
        for addr, node in self.functions.items():
            if node.name:
                name_lower = node.name.lower()
                if any(kw in name_lower for kw in rt_keywords):
                    rt_funcs[addr] = node
        
        return rt_funcs
    
    def to_json(self) -> str:
        """Export as JSON."""
        return json.dumps({
            'functions': {
                hex(addr): {
                    'offset': hex(node.offset),
                    'name': node.name,
                    'calls': [hex(c) for c in node.calls],
                    'called_by': [hex(c) for c in node.called_by]
                }
                for addr, node in self.functions.items()
            },
            'calls': [
                {
                    'caller': hex(c.caller_offset),
                    'callee': hex(c.callee_offset),
                    'callee_name': c.callee_name
                }
                for c in self.calls
            ]
        }, indent=2)
    
    def print_graph(self, filter_rt: bool = False):
        """Print call graph."""
        funcs = self.get_rt_subgraph() if filter_rt else self.functions
        
        print("\n" + "=" * 70)
        print("CALL GRAPH")
        print("=" * 70)
        
        # Find root functions (not called by others)
        roots = [addr for addr, node in funcs.items() if not node.called_by]
        
        def print_tree(addr: int, indent: int = 0, visited: Set[int] = None):
            if visited is None:
                visited = set()
            
            if addr in visited or addr not in self.functions:
                return
            
            visited.add(addr)
            node = self.functions[addr]
            
            prefix = "  " * indent
            name = node.name or f"sub_{node.offset:X}"
            # Truncate long names
            if len(name) > 50:
                name = name[:47] + "..."
            
            print(f"{prefix}├─ {name}")
            print(f"{prefix}│  @ 0x{node.offset:X}")
            
            for callee in node.calls[:10]:  # Limit callees shown
                if callee in funcs:
                    print_tree(callee, indent + 1, visited)
            
            if len(node.calls) > 10:
                print(f"{prefix}│  ... and {len(node.calls) - 10} more calls")
        
        for root in roots[:5]:
            print_tree(root)
        
        print("\n" + "=" * 70)
        print(f"Total: {len(funcs)} functions")


def main():
    parser = argparse.ArgumentParser(description="Trace function call graph")
    parser.add_argument("binary", help="Path to Cyberpunk2077 binary")
    parser.add_argument("--start", "-s", type=lambda x: int(x, 0),
                       help="Starting offset (hex)")
    parser.add_argument("--symbol", help="Find symbol containing this string")
    parser.add_argument("--depth", "-d", type=int, default=3,
                       help="Max trace depth (default: 3)")
    parser.add_argument("--json", "-j", action="store_true", help="Output JSON")
    parser.add_argument("--rt-only", action="store_true", 
                       help="Show only RT-related functions")
    
    args = parser.parse_args()
    
    tracer = CallGraphTracer(args.binary)
    tracer.load()
    
    start_offset = args.start
    if args.symbol:
        addr = tracer.find_symbol_address(args.symbol)
        if addr:
            start_offset = addr - IMAGE_BASE
            print(f"[*] Found symbol at 0x{start_offset:X}")
        else:
            print(f"[!] Symbol not found: {args.symbol}")
            return
    
    if start_offset is None:
        print("[!] Please provide --start or --symbol")
        return
    
    tracer.trace_calls(start_offset, args.depth)
    
    if args.json:
        print(tracer.to_json())
    else:
        tracer.print_graph(args.rt_only)


if __name__ == "__main__":
    main()
