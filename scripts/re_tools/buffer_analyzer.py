#!/usr/bin/env python3
"""
GPU Buffer Analyzer

Analyzes GPU buffer naming patterns and memory pools in the binary.
Focuses on ray tracing and graphics pipeline buffers.

Usage:
    python3 buffer_analyzer.py /path/to/Cyberpunk2077
    python3 buffer_analyzer.py /path/to/Cyberpunk2077 --apple-only
"""

import subprocess
import re
import json
from pathlib import Path
from dataclasses import dataclass, field, asdict
from typing import List, Dict, Optional, Set
from collections import defaultdict
import argparse


@dataclass
class GPUBuffer:
    """A GPU buffer found in the binary."""
    name: str
    category: str
    is_apple_specific: bool = False
    is_input: bool = False
    is_output: bool = False
    is_temp: bool = False
    related_render_node: Optional[str] = None
    memory_pool: Optional[str] = None


@dataclass
class MemoryPool:
    """A GPU memory pool."""
    name: str
    category: str
    related_buffers: List[str] = field(default_factory=list)


class BufferAnalyzer:
    """Analyzes GPU buffers in Cyberpunk 2077."""
    
    def __init__(self, binary_path: str):
        self.binary_path = Path(binary_path)
        if not self.binary_path.exists():
            raise FileNotFoundError(f"Binary not found: {binary_path}")
        
        self.buffers: Dict[str, GPUBuffer] = {}
        self.pools: Dict[str, MemoryPool] = {}
        self.all_strings: List[str] = []
        
    def analyze(self) -> Dict[str, GPUBuffer]:
        """Run full analysis."""
        print("[*] Extracting strings...")
        self._extract_strings()
        
        print("[*] Finding buffers...")
        self._find_buffers()
        
        print("[*] Finding memory pools...")
        self._find_pools()
        
        print("[*] Categorizing...")
        self._categorize_buffers()
        
        print(f"[+] Found {len(self.buffers)} buffers, {len(self.pools)} pools")
        return self.buffers
    
    def _extract_strings(self):
        """Extract strings from binary."""
        try:
            result = subprocess.run(
                ['strings', '-n', '4', str(self.binary_path)],
                capture_output=True,
                text=True,
                timeout=120
            )
            self.all_strings = result.stdout.split('\n')
        except subprocess.TimeoutExpired:
            print("[!] Timeout")
    
    def _find_buffers(self):
        """Find GPU buffer names."""
        buffer_patterns = [
            # Apple-specific
            (r'aapl\w+Buffer\b', True),
            (r'aapl\w+\b', True),
            (r'm_aapl\w+\b', True),
            
            # General buffers
            (r'm_\w+Buffer\b', False),
            (r'\w+OutputBuffer\b', False),
            (r'\w+InputBuffer\b', False),
            (r'\w+TempBuffer\b', False),
            (r'\w+Accumulation\d*\b', False),
            (r'\w+Variance\d*\b', False),
            
            # RT-specific
            (r'rtxdi\w+\b', False),
            (r'restir\w+\b', False),
            (r'nrd\w+\b', False),
            (r'denois\w+\b', False),
            (r'reservoir\w+\b', False),
        ]
        
        for s in self.all_strings:
            s = s.strip()
            for pattern, is_apple in buffer_patterns:
                if re.match(pattern, s, re.IGNORECASE):
                    if s not in self.buffers and len(s) < 100:
                        self.buffers[s] = GPUBuffer(
                            name=s,
                            category='Unknown',
                            is_apple_specific=is_apple
                        )
                        break
    
    def _find_pools(self):
        """Find memory pool definitions."""
        pool_pattern = re.compile(r'Pool\w+|GPUM_\w+')
        
        for s in self.all_strings:
            s = s.strip()
            if pool_pattern.match(s):
                if s not in self.pools and len(s) < 80:
                    category = 'General'
                    if 'raytrac' in s.lower():
                        category = 'RayTracing'
                    elif 'metal' in s.lower():
                        category = 'Metal'
                    
                    self.pools[s] = MemoryPool(name=s, category=category)
    
    def _categorize_buffers(self):
        """Categorize buffers by purpose."""
        for name, buf in self.buffers.items():
            name_lower = name.lower()
            
            # Determine I/O type
            buf.is_input = 'input' in name_lower
            buf.is_output = 'output' in name_lower
            buf.is_temp = 'temp' in name_lower or 'tmp' in name_lower
            
            # Categorize by content
            if 'rtxdi' in name_lower:
                buf.category = 'RTXDI'
            elif 'restir' in name_lower or 'reservoir' in name_lower:
                buf.category = 'ReSTIR'
            elif 'denois' in name_lower or 'nrd' in name_lower:
                buf.category = 'Denoiser'
            elif 'shadow' in name_lower:
                buf.category = 'Shadows'
            elif 'gi' in name_lower or 'diffuse' in name_lower:
                buf.category = 'GI'
            elif 'specular' in name_lower or 'reflect' in name_lower:
                buf.category = 'Reflections'
            elif 'ray' in name_lower:
                buf.category = 'RayTracing'
            elif 'accel' in name_lower or 'bvh' in name_lower:
                buf.category = 'AccelerationStructure'
            elif 'variance' in name_lower or 'accumulation' in name_lower:
                buf.category = 'Temporal'
            else:
                buf.category = 'Other'
    
    def get_apple_buffers(self) -> List[GPUBuffer]:
        """Get Apple-specific buffers."""
        return [b for b in self.buffers.values() if b.is_apple_specific]
    
    def get_by_category(self) -> Dict[str, List[GPUBuffer]]:
        """Group by category."""
        categories = defaultdict(list)
        for buf in self.buffers.values():
            categories[buf.category].append(buf)
        return dict(categories)
    
    def to_json(self) -> str:
        """Export as JSON."""
        return json.dumps({
            'buffers': {name: asdict(buf) for name, buf in self.buffers.items()},
            'pools': {name: asdict(pool) for name, pool in self.pools.items()}
        }, indent=2)
    
    def print_summary(self, apple_only: bool = False):
        """Print summary."""
        print("\n" + "=" * 70)
        print("GPU BUFFER ANALYSIS")
        print("=" * 70)
        
        if apple_only:
            print("\n### Apple-Specific Buffers (AAPL)")
            print("-" * 50)
            for buf in sorted(self.get_apple_buffers(), key=lambda x: x.name):
                io_str = []
                if buf.is_input: io_str.append("IN")
                if buf.is_output: io_str.append("OUT")
                if buf.is_temp: io_str.append("TEMP")
                io_suffix = f" [{'/'.join(io_str)}]" if io_str else ""
                print(f"  {buf.name} ({buf.category}){io_suffix}")
        else:
            categories = self.get_by_category()
            priority = ['RayTracing', 'RTXDI', 'ReSTIR', 'GI', 'Reflections', 
                       'Shadows', 'Denoiser', 'Temporal', 'AccelerationStructure']
            
            def sort_key(cat):
                try:
                    return priority.index(cat)
                except ValueError:
                    return 100
            
            for cat in sorted(categories.keys(), key=sort_key):
                buffers = categories[cat]
                apple_count = sum(1 for b in buffers if b.is_apple_specific)
                apple_str = f" ({apple_count} Apple-specific)" if apple_count else ""
                
                print(f"\n### {cat}{apple_str}")
                print("-" * 50)
                
                for buf in sorted(buffers, key=lambda x: x.name)[:15]:
                    prefix = "[AAPL] " if buf.is_apple_specific else "       "
                    print(f"  {prefix}{buf.name}")
                
                if len(buffers) > 15:
                    print(f"  ... and {len(buffers) - 15} more")
        
        print("\n### Memory Pools")
        print("-" * 50)
        for pool in sorted(self.pools.values(), key=lambda x: x.name):
            print(f"  {pool.name} ({pool.category})")
        
        print("\n" + "=" * 70)
        apple_total = len(self.get_apple_buffers())
        print(f"Total: {len(self.buffers)} buffers ({apple_total} Apple-specific)")
        print(f"Memory Pools: {len(self.pools)}")


def main():
    parser = argparse.ArgumentParser(description="Analyze GPU buffers")
    parser.add_argument("binary", help="Path to Cyberpunk2077 binary")
    parser.add_argument("--apple-only", "-a", action="store_true", 
                       help="Show only Apple-specific buffers")
    parser.add_argument("--json", "-j", action="store_true", help="Output as JSON")
    
    args = parser.parse_args()
    
    analyzer = BufferAnalyzer(args.binary)
    analyzer.analyze()
    
    if args.json:
        print(analyzer.to_json())
    else:
        analyzer.print_summary(args.apple_only)


if __name__ == "__main__":
    main()
