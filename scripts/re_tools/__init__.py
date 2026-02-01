"""
Cyberpunk 2077 Reverse Engineering Tools

A suite of tools for analyzing the Cyberpunk 2077 macOS binary,
focused on ray tracing and graphics pipeline reverse engineering.

Tools:
    - render_node_analyzer: Find CRenderNode_* classes and methods
    - cvar_scanner: Extract console variables
    - buffer_analyzer: Analyze GPU buffer patterns
    - call_graph_tracer: Trace function call relationships

Usage:
    from re_tools import RenderNodeAnalyzer, CVarScanner, BufferAnalyzer

    analyzer = RenderNodeAnalyzer("/path/to/Cyberpunk2077")
    nodes = analyzer.analyze()
"""

from .render_node_analyzer import RenderNodeAnalyzer, RenderNode
from .cvar_scanner import CVarScanner, CVar
from .buffer_analyzer import BufferAnalyzer, GPUBuffer
from .call_graph_tracer import CallGraphTracer, FunctionNode

__all__ = [
    'RenderNodeAnalyzer', 'RenderNode',
    'CVarScanner', 'CVar',
    'BufferAnalyzer', 'GPUBuffer',
    'CallGraphTracer', 'FunctionNode',
]

__version__ = '0.1.0'
