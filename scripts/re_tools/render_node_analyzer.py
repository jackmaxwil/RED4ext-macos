#!/usr/bin/env python3
"""
Render Node Analyzer

Extracts all CRenderNode_* classes, their vtables, and methods from the binary.
Focused on ray tracing and graphics pipeline analysis.

Usage:
    python3 render_node_analyzer.py /path/to/Cyberpunk2077
    python3 render_node_analyzer.py /path/to/Cyberpunk2077 --filter "RayTrac"
    python3 render_node_analyzer.py /path/to/Cyberpunk2077 --json > nodes.json
"""

import subprocess
import re
import json
import struct
from pathlib import Path
from dataclasses import dataclass, field, asdict
from typing import List, Dict, Optional, Set
from collections import defaultdict
import argparse

IMAGE_BASE = 0x100000000


@dataclass
class RenderNodeMethod:
    """A method belonging to a render node class."""
    name: str
    address: int
    offset: int
    vtable_index: Optional[int] = None
    signature: Optional[str] = None
    
    @property
    def hex_offset(self) -> str:
        return f"0x{self.offset:X}"


@dataclass
class RenderNode:
    """A CRenderNode class found in the binary."""
    class_name: str
    name_string_addr: Optional[int] = None
    vtable_addr: Optional[int] = None
    methods: List[RenderNodeMethod] = field(default_factory=list)
    related_buffers: List[str] = field(default_factory=list)
    related_strings: List[str] = field(default_factory=list)
    
    @property
    def category(self) -> str:
        """Categorize the render node."""
        name = self.class_name.lower()
        if 'raytrac' in name:
            if 'shadow' in name:
                return 'RT_Shadows'
            elif 'reflect' in name:
                return 'RT_Reflections'
            elif 'restir' in name:
                return 'RT_ReSTIR_GI'
            elif 'rtxdi' in name:
                return 'RT_RTXDI'
            elif 'ao' in name or 'ambient' in name:
                return 'RT_AO'
            else:
                return 'RT_Other'
        elif 'fsr' in name:
            return 'Upscaling_FSR'
        elif 'upscal' in name:
            return 'Upscaling_Other'
        elif 'accel' in name:
            return 'AccelerationStructure'
        elif 'denois' in name or 'filter' in name:
            return 'Denoising'
        else:
            return 'Other'


class RenderNodeAnalyzer:
    """Analyzes render nodes in Cyberpunk 2077 binary."""
    
    def __init__(self, binary_path: str):
        self.binary_path = Path(binary_path)
        if not self.binary_path.exists():
            raise FileNotFoundError(f"Binary not found: {binary_path}")
        
        self.nodes: Dict[str, RenderNode] = {}
        self.symbols: Dict[str, int] = {}
        self.strings: Dict[int, str] = {}
        
    def analyze(self) -> Dict[str, RenderNode]:
        """Run full analysis pipeline."""
        print("[*] Loading symbols...")
        self._load_symbols()
        
        print("[*] Finding render node classes...")
        self._find_render_nodes()
        
        print("[*] Finding related strings...")
        self._find_related_strings()
        
        print("[*] Finding related buffers...")
        self._find_related_buffers()
        
        print(f"[+] Found {len(self.nodes)} render nodes")
        return self.nodes
    
    def _load_symbols(self):
        """Load symbols from binary using nm."""
        try:
            result = subprocess.run(
                ['nm', '-n', str(self.binary_path)],
                capture_output=True,
                text=True,
                timeout=120
            )
            
            for line in result.stdout.split('\n'):
                parts = line.split()
                if len(parts) >= 3:
                    try:
                        addr = int(parts[0], 16)
                        sym_type = parts[1]
                        symbol = ' '.join(parts[2:])
                        self.symbols[symbol] = addr
                    except ValueError:
                        continue
                        
        except subprocess.TimeoutExpired:
            print("[!] Symbol loading timed out")
    
    def _find_render_nodes(self):
        """Find all CRenderNode_* classes."""
        # Find from string constants
        try:
            result = subprocess.run(
                ['strings', str(self.binary_path)],
                capture_output=True,
                text=True,
                timeout=60
            )
            
            for line in result.stdout.split('\n'):
                if line.startswith('CRenderNode_'):
                    class_name = line.strip()
                    if class_name not in self.nodes:
                        self.nodes[class_name] = RenderNode(class_name=class_name)
                        
        except subprocess.TimeoutExpired:
            print("[!] String search timed out")
        
        # Find from symbols
        for symbol, addr in self.symbols.items():
            match = re.search(r'(CRenderNode_\w+)', symbol)
            if match:
                class_name = match.group(1)
                if class_name not in self.nodes:
                    self.nodes[class_name] = RenderNode(class_name=class_name)
                
                # Check if this is a method
                node = self.nodes[class_name]
                if '::' in symbol:
                    method_match = re.search(r'::(\w+)\(', symbol)
                    if method_match:
                        method_name = method_match.group(1)
                        offset = addr - IMAGE_BASE if addr >= IMAGE_BASE else addr
                        node.methods.append(RenderNodeMethod(
                            name=method_name,
                            address=addr,
                            offset=offset
                        ))
    
    def _find_related_strings(self):
        """Find strings related to each render node."""
        try:
            result = subprocess.run(
                ['strings', str(self.binary_path)],
                capture_output=True,
                text=True,
                timeout=60
            )
            
            all_strings = result.stdout.split('\n')
            
            for node in self.nodes.values():
                # Extract key words from class name
                words = re.findall(r'[A-Z][a-z]+', node.class_name)
                key_terms = [w.lower() for w in words if len(w) > 3]
                
                for s in all_strings:
                    s_lower = s.lower()
                    if any(term in s_lower for term in key_terms):
                        if len(s) < 100 and s not in node.related_strings:
                            node.related_strings.append(s)
                            if len(node.related_strings) > 20:
                                break
                                
        except subprocess.TimeoutExpired:
            pass
    
    def _find_related_buffers(self):
        """Find AAPL buffers and other graphics buffers."""
        try:
            result = subprocess.run(
                ['strings', str(self.binary_path)],
                capture_output=True,
                text=True,
                timeout=60
            )
            
            buffer_patterns = [
                r'aapl\w+Buffer',
                r'm_\w+Buffer',
                r'\w+OutputBuffer',
                r'\w+InputBuffer',
            ]
            
            all_buffers = set()
            for line in result.stdout.split('\n'):
                for pattern in buffer_patterns:
                    if re.search(pattern, line, re.IGNORECASE):
                        all_buffers.add(line.strip())
            
            # Associate buffers with nodes
            for node in self.nodes.values():
                words = re.findall(r'[A-Z][a-z]+', node.class_name)
                key_terms = [w.lower() for w in words if len(w) > 3]
                
                for buf in all_buffers:
                    buf_lower = buf.lower()
                    if any(term in buf_lower for term in key_terms):
                        if buf not in node.related_buffers:
                            node.related_buffers.append(buf)
                            
        except subprocess.TimeoutExpired:
            pass
    
    def get_by_category(self) -> Dict[str, List[RenderNode]]:
        """Group render nodes by category."""
        categories = defaultdict(list)
        for node in self.nodes.values():
            categories[node.category].append(node)
        return dict(categories)
    
    def to_json(self) -> str:
        """Export analysis as JSON."""
        return json.dumps(
            {name: asdict(node) for name, node in self.nodes.items()},
            indent=2
        )
    
    def print_summary(self, filter_str: Optional[str] = None):
        """Print human-readable summary."""
        categories = self.get_by_category()
        
        print("\n" + "=" * 70)
        print("RENDER NODE ANALYSIS SUMMARY")
        print("=" * 70)
        
        for category, nodes in sorted(categories.items()):
            if filter_str and filter_str.lower() not in category.lower():
                # Check if any node matches filter
                nodes = [n for n in nodes if filter_str.lower() in n.class_name.lower()]
                if not nodes:
                    continue
            
            print(f"\n### {category} ({len(nodes)} nodes)")
            print("-" * 50)
            
            for node in sorted(nodes, key=lambda x: x.class_name):
                print(f"\n  {node.class_name}")
                
                if node.methods:
                    print(f"    Methods ({len(node.methods)}):")
                    for method in node.methods[:5]:
                        print(f"      - {method.name}() @ {method.hex_offset}")
                    if len(node.methods) > 5:
                        print(f"      ... and {len(node.methods) - 5} more")
                
                if node.related_buffers:
                    print(f"    Buffers: {', '.join(node.related_buffers[:3])}")
        
        print("\n" + "=" * 70)


def main():
    parser = argparse.ArgumentParser(description="Analyze render nodes in Cyberpunk 2077")
    parser.add_argument("binary", help="Path to Cyberpunk2077 binary")
    parser.add_argument("--filter", "-f", help="Filter by string (e.g., 'RayTrac')")
    parser.add_argument("--json", "-j", action="store_true", help="Output as JSON")
    parser.add_argument("--category", "-c", help="Show only specific category")
    
    args = parser.parse_args()
    
    analyzer = RenderNodeAnalyzer(args.binary)
    analyzer.analyze()
    
    if args.json:
        print(analyzer.to_json())
    else:
        analyzer.print_summary(args.filter or args.category)


if __name__ == "__main__":
    main()
