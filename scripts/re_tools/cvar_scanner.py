#!/usr/bin/env python3
"""
Console Variable (CVar) Scanner

Extracts all cvars (console variables) from the binary, particularly those
related to ray tracing, path tracing, and graphics configuration.

Usage:
    python3 cvar_scanner.py /path/to/Cyberpunk2077
    python3 cvar_scanner.py /path/to/Cyberpunk2077 --filter "raytracing"
    python3 cvar_scanner.py /path/to/Cyberpunk2077 --category graphics
"""

import subprocess
import re
import json
from pathlib import Path
from dataclasses import dataclass, asdict
from typing import List, Dict, Optional, Tuple
from collections import defaultdict
import argparse


@dataclass
class CVar:
    """A console variable found in the binary."""
    name: str
    category: str
    value_type: Optional[str] = None
    default_value: Optional[str] = None
    description: Optional[str] = None
    related_strings: List[str] = None
    
    def __post_init__(self):
        if self.related_strings is None:
            self.related_strings = []


class CVarScanner:
    """Scans Cyberpunk 2077 binary for console variables."""
    
    # Known CVar prefixes and their categories
    CVAR_PREFIXES = {
        'cv': 'General',
        'cvRayTracing': 'RayTracing',
        'cvPathTracing': 'PathTracing', 
        'cvRTXDI': 'RTXDI',
        'cvReSTIR': 'ReSTIR',
        'cvFSR': 'FSR',
        'cvUpscal': 'Upscaling',
        'cvDenois': 'Denoising',
        'cvNRD': 'NRD',
        'cvRender': 'Rendering',
        'cvGPU': 'GPU',
        'cvMetal': 'Metal',
        'cvShader': 'Shaders',
        'cvAS': 'AccelerationStructure',
        'cvBVH': 'BVH',
    }
    
    def __init__(self, binary_path: str):
        self.binary_path = Path(binary_path)
        if not self.binary_path.exists():
            raise FileNotFoundError(f"Binary not found: {binary_path}")
        
        self.cvars: Dict[str, CVar] = {}
        self.all_strings: List[str] = []
        
    def scan(self) -> Dict[str, CVar]:
        """Run full scan pipeline."""
        print("[*] Extracting strings...")
        self._extract_strings()
        
        print("[*] Finding cvars...")
        self._find_cvars()
        
        print("[*] Inferring types...")
        self._infer_types()
        
        print(f"[+] Found {len(self.cvars)} cvars")
        return self.cvars
    
    def _extract_strings(self):
        """Extract all strings from binary."""
        try:
            result = subprocess.run(
                ['strings', '-n', '4', str(self.binary_path)],
                capture_output=True,
                text=True,
                timeout=120
            )
            self.all_strings = result.stdout.split('\n')
        except subprocess.TimeoutExpired:
            print("[!] String extraction timed out")
    
    def _find_cvars(self):
        """Find all console variables."""
        # Pattern for cvars: cv followed by PascalCase
        cvar_pattern = re.compile(r'^cv[A-Z][A-Za-z0-9_]+$')
        
        for s in self.all_strings:
            s = s.strip()
            if cvar_pattern.match(s):
                if s not in self.cvars:
                    category = self._categorize_cvar(s)
                    self.cvars[s] = CVar(name=s, category=category)
        
        # Also find cvars with different patterns
        setting_patterns = [
            r'^(Enable|Disable|Use|Allow)[A-Z][A-Za-z0-9]+$',
            r'^Max[A-Z][A-Za-z0-9]+$',
            r'^Min[A-Z][A-Za-z0-9]+$',
            r'^Num[A-Z][A-Za-z0-9]+$',
        ]
        
        for s in self.all_strings:
            s = s.strip()
            for pattern in setting_patterns:
                if re.match(pattern, s):
                    # Check if it's RT/graphics related
                    s_lower = s.lower()
                    if any(kw in s_lower for kw in ['ray', 'rtxdi', 'restir', 'denois', 'shadow', 'reflect']):
                        if s not in self.cvars:
                            self.cvars[s] = CVar(name=s, category='Settings')
    
    def _categorize_cvar(self, name: str) -> str:
        """Categorize a cvar based on its name."""
        name_lower = name.lower()
        
        # Check specific prefixes first
        for prefix, category in sorted(self.CVAR_PREFIXES.items(), key=lambda x: -len(x[0])):
            if name_lower.startswith(prefix.lower()):
                return category
        
        # Keyword-based categorization
        keywords = {
            'RayTracing': ['raytrac', 'ray_trac'],
            'PathTracing': ['pathtrac', 'path_trac'],
            'RTXDI': ['rtxdi'],
            'ReSTIR': ['restir'],
            'FSR': ['fsr'],
            'Denoising': ['denois', 'nrd', 'temporal', 'spatial'],
            'Shadows': ['shadow'],
            'Reflections': ['reflect'],
            'GI': ['gi', 'globalillum'],
            'AO': ['ao', 'ambient'],
            'Upscaling': ['upscal', 'metalfx'],
        }
        
        for category, kws in keywords.items():
            if any(kw in name_lower for kw in kws):
                return category
        
        return 'General'
    
    def _infer_types(self):
        """Try to infer cvar types from naming conventions."""
        for name, cvar in self.cvars.items():
            name_lower = name.lower()
            
            if name_lower.startswith(('enable', 'disable', 'use', 'allow', 'is')):
                cvar.value_type = 'bool'
            elif name_lower.startswith(('num', 'count', 'max', 'min')):
                cvar.value_type = 'int'
            elif 'scale' in name_lower or 'factor' in name_lower or 'ratio' in name_lower:
                cvar.value_type = 'float'
            elif 'color' in name_lower:
                cvar.value_type = 'color'
            elif 'distance' in name_lower or 'range' in name_lower:
                cvar.value_type = 'float'
            elif 'quality' in name_lower:
                cvar.value_type = 'enum/int'
    
    def get_by_category(self) -> Dict[str, List[CVar]]:
        """Group cvars by category."""
        categories = defaultdict(list)
        for cvar in self.cvars.values():
            categories[cvar.category].append(cvar)
        return dict(categories)
    
    def get_rt_cvars(self) -> List[CVar]:
        """Get all ray tracing related cvars."""
        rt_categories = {'RayTracing', 'PathTracing', 'RTXDI', 'ReSTIR', 'Shadows', 
                        'Reflections', 'GI', 'AO', 'Denoising', 'NRD'}
        return [cv for cv in self.cvars.values() if cv.category in rt_categories]
    
    def to_json(self) -> str:
        """Export as JSON."""
        return json.dumps(
            {name: asdict(cvar) for name, cvar in self.cvars.items()},
            indent=2
        )
    
    def print_summary(self, filter_str: Optional[str] = None, category: Optional[str] = None):
        """Print human-readable summary."""
        categories = self.get_by_category()
        
        print("\n" + "=" * 70)
        print("CONSOLE VARIABLE SCAN RESULTS")
        print("=" * 70)
        
        # Priority categories for RT/PT analysis
        priority_order = ['RayTracing', 'PathTracing', 'RTXDI', 'ReSTIR', 'Denoising', 
                         'NRD', 'Shadows', 'Reflections', 'GI', 'AO', 'FSR', 'Upscaling']
        
        def sort_key(cat):
            try:
                return priority_order.index(cat)
            except ValueError:
                return 100
        
        for cat in sorted(categories.keys(), key=sort_key):
            cvars = categories[cat]
            
            if category and category.lower() not in cat.lower():
                continue
            
            if filter_str:
                cvars = [cv for cv in cvars if filter_str.lower() in cv.name.lower()]
                if not cvars:
                    continue
            
            print(f"\n### {cat} ({len(cvars)} cvars)")
            print("-" * 50)
            
            for cvar in sorted(cvars, key=lambda x: x.name):
                type_str = f" [{cvar.value_type}]" if cvar.value_type else ""
                print(f"  {cvar.name}{type_str}")
        
        print("\n" + "=" * 70)
        print(f"Total: {len(self.cvars)} console variables")


def main():
    parser = argparse.ArgumentParser(description="Scan for console variables")
    parser.add_argument("binary", help="Path to Cyberpunk2077 binary")
    parser.add_argument("--filter", "-f", help="Filter by string")
    parser.add_argument("--category", "-c", help="Show only specific category")
    parser.add_argument("--json", "-j", action="store_true", help="Output as JSON")
    parser.add_argument("--rt-only", action="store_true", help="Show only RT-related cvars")
    
    args = parser.parse_args()
    
    scanner = CVarScanner(args.binary)
    scanner.scan()
    
    if args.json:
        print(scanner.to_json())
    elif args.rt_only:
        print("\nRay Tracing Related CVars:")
        print("-" * 50)
        for cvar in sorted(scanner.get_rt_cvars(), key=lambda x: x.name):
            type_str = f" [{cvar.value_type}]" if cvar.value_type else ""
            print(f"  {cvar.name}{type_str}")
    else:
        scanner.print_summary(args.filter, args.category)


if __name__ == "__main__":
    main()
