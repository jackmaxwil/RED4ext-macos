#!/usr/bin/env python3
"""
Comprehensive RT/PT Pipeline Analyzer

Runs all RE tools and generates a comprehensive report on the
ray tracing and path tracing implementation.

Usage:
    python3 analyze_rt_pipeline.py /path/to/Cyberpunk2077
    python3 analyze_rt_pipeline.py /path/to/Cyberpunk2077 --output report.json
"""

import sys
import json
import argparse
from pathlib import Path
from datetime import datetime
from typing import Dict, Any

# Add parent to path for imports
sys.path.insert(0, str(Path(__file__).parent.parent))

from re_tools.render_node_analyzer import RenderNodeAnalyzer
from re_tools.cvar_scanner import CVarScanner
from re_tools.buffer_analyzer import BufferAnalyzer


class RTPipelineAnalyzer:
    """Comprehensive RT/PT pipeline analysis."""
    
    def __init__(self, binary_path: str):
        self.binary_path = Path(binary_path)
        self.report: Dict[str, Any] = {
            'metadata': {
                'binary': str(self.binary_path),
                'timestamp': datetime.now().isoformat(),
                'version': '0.1.0'
            },
            'render_nodes': {},
            'cvars': {},
            'buffers': {},
            'summary': {}
        }
        
    def analyze_all(self) -> Dict[str, Any]:
        """Run all analyses."""
        binary = str(self.binary_path)
        
        # Render Nodes
        print("\n" + "=" * 70)
        print("PHASE 1: RENDER NODE ANALYSIS")
        print("=" * 70)
        
        node_analyzer = RenderNodeAnalyzer(binary)
        nodes = node_analyzer.analyze()
        
        rt_nodes = [n for n in nodes.values() if 'RT_' in n.category or 'Denois' in n.category]
        self.report['render_nodes'] = {
            'total': len(nodes),
            'rt_related': len(rt_nodes),
            'by_category': {cat: len(items) for cat, items in node_analyzer.get_by_category().items()},
            'rt_nodes': [n.class_name for n in rt_nodes]
        }
        
        # CVars
        print("\n" + "=" * 70)
        print("PHASE 2: CONSOLE VARIABLE SCAN")
        print("=" * 70)
        
        cvar_scanner = CVarScanner(binary)
        cvars = cvar_scanner.scan()
        
        rt_cvars = cvar_scanner.get_rt_cvars()
        self.report['cvars'] = {
            'total': len(cvars),
            'rt_related': len(rt_cvars),
            'by_category': {cat: len(items) for cat, items in cvar_scanner.get_by_category().items()},
            'rt_cvars': [cv.name for cv in rt_cvars]
        }
        
        # Buffers
        print("\n" + "=" * 70)
        print("PHASE 3: GPU BUFFER ANALYSIS")
        print("=" * 70)
        
        buffer_analyzer = BufferAnalyzer(binary)
        buffers = buffer_analyzer.analyze()
        
        apple_buffers = buffer_analyzer.get_apple_buffers()
        self.report['buffers'] = {
            'total': len(buffers),
            'apple_specific': len(apple_buffers),
            'by_category': {cat: len(items) for cat, items in buffer_analyzer.get_by_category().items()},
            'apple_buffers': [b.name for b in apple_buffers],
            'pools': list(buffer_analyzer.pools.keys())
        }
        
        # Summary
        self._generate_summary()
        
        return self.report
    
    def _generate_summary(self):
        """Generate analysis summary."""
        self.report['summary'] = {
            'rt_implementation': {
                'has_apple_specific_code': self.report['buffers']['apple_specific'] > 0,
                'apple_buffer_count': self.report['buffers']['apple_specific'],
                'rt_render_nodes': self.report['render_nodes']['rt_related'],
                'rt_cvars': self.report['cvars']['rt_related'],
            },
            'optimization_opportunities': self._identify_opportunities(),
            'key_findings': self._key_findings()
        }
    
    def _identify_opportunities(self) -> list:
        """Identify potential optimization areas."""
        opportunities = []
        
        # Check for Apple-specific implementation depth
        apple_count = self.report['buffers']['apple_specific']
        if apple_count > 10:
            opportunities.append({
                'area': 'Apple Integration',
                'finding': f'Found {apple_count} Apple-specific buffers',
                'implication': 'Significant platform-specific implementation exists'
            })
        
        # Check for denoiser buffers
        buf_cats = self.report['buffers']['by_category']
        if buf_cats.get('Denoiser', 0) > 5:
            opportunities.append({
                'area': 'Denoising',
                'finding': f'{buf_cats["Denoiser"]} denoiser buffers found',
                'implication': 'Complex denoising pipeline - potential for optimization'
            })
        
        # Check for RTXDI/ReSTIR complexity
        if buf_cats.get('RTXDI', 0) > 0 or buf_cats.get('ReSTIR', 0) > 0:
            opportunities.append({
                'area': 'Light Sampling',
                'finding': 'RTXDI/ReSTIR implementation present',
                'implication': 'Advanced light sampling - may need Apple Silicon tuning'
            })
        
        return opportunities
    
    def _key_findings(self) -> list:
        """Compile key findings."""
        findings = []
        
        rt_nodes = self.report['render_nodes'].get('rt_nodes', [])
        if rt_nodes:
            findings.append(f"Ray Tracing Nodes: {', '.join(rt_nodes[:5])}")
        
        apple_bufs = self.report['buffers'].get('apple_buffers', [])
        if apple_bufs:
            findings.append(f"Apple Buffers: {', '.join(apple_bufs[:5])}")
        
        return findings
    
    def print_report(self):
        """Print human-readable report."""
        print("\n" + "=" * 70)
        print("RT/PT PIPELINE ANALYSIS REPORT")
        print("=" * 70)
        
        print(f"\nBinary: {self.report['metadata']['binary']}")
        print(f"Analyzed: {self.report['metadata']['timestamp']}")
        
        # Render Nodes
        rn = self.report['render_nodes']
        print(f"\n### Render Nodes")
        print(f"  Total: {rn['total']}")
        print(f"  RT-Related: {rn['rt_related']}")
        print(f"  Categories: {rn['by_category']}")
        
        # CVars
        cv = self.report['cvars']
        print(f"\n### Console Variables")
        print(f"  Total: {cv['total']}")
        print(f"  RT-Related: {cv['rt_related']}")
        
        # Buffers
        bf = self.report['buffers']
        print(f"\n### GPU Buffers")
        print(f"  Total: {bf['total']}")
        print(f"  Apple-Specific: {bf['apple_specific']}")
        print(f"  Memory Pools: {len(bf['pools'])}")
        
        # Summary
        print("\n### Key Findings")
        for finding in self.report['summary']['key_findings']:
            print(f"  • {finding}")
        
        print("\n### Optimization Opportunities")
        for opp in self.report['summary']['optimization_opportunities']:
            print(f"  [{opp['area']}]")
            print(f"    Finding: {opp['finding']}")
            print(f"    Implication: {opp['implication']}")
        
        print("\n" + "=" * 70)


def main():
    parser = argparse.ArgumentParser(description="Analyze RT/PT pipeline")
    parser.add_argument("binary", help="Path to Cyberpunk2077 binary")
    parser.add_argument("--output", "-o", help="Output JSON file")
    parser.add_argument("--json", "-j", action="store_true", help="Print JSON to stdout")
    
    args = parser.parse_args()
    
    analyzer = RTPipelineAnalyzer(args.binary)
    report = analyzer.analyze_all()
    
    if args.output:
        with open(args.output, 'w') as f:
            json.dump(report, f, indent=2)
        print(f"\n[+] Report saved to {args.output}")
    
    if args.json:
        print(json.dumps(report, indent=2))
    else:
        analyzer.print_report()


if __name__ == "__main__":
    main()
