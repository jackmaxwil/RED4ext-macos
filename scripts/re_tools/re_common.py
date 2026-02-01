#!/usr/bin/env python3
"""
Shared data structures for reverse engineering pipeline scripts.

Used by static_analysis.py, dynamic_analysis.py, and merge_results.py
to ensure consistent data structures across the pipeline.
"""

from dataclasses import dataclass, field
from typing import Optional, List, Dict

# Constants
IMAGE_BASE = 0x100000000
TEXT_SEGMENT = 1


@dataclass
class FunctionInfo:
    """Information about a discovered function."""
    offset: int
    address: int
    name: Optional[str] = None
    confidence: str = "low"  # "low", "medium", "high"
    discovery_method: str = "static"  # "static", "dynamic", "both"
    call_count: int = 0
    callers: List[int] = field(default_factory=list)
    callees: List[int] = field(default_factory=list)
    signature: Optional[Dict] = None
    prologue_type: Optional[str] = None  # For static analysis
    
    def to_dict(self) -> Dict:
        """Convert to dictionary for JSON export."""
        return {
            "offset": f"0x{self.offset:X}",
            "address": f"0x{self.address:X}",
            "name": self.name,
            "confidence": self.confidence,
            "discovery_method": self.discovery_method,
            "call_count": self.call_count,
            "callers": [f"0x{c:X}" for c in self.callers],
            "callees": [f"0x{c:X}" for c in self.callees],
            "signature": self.signature,
            "prologue_type": self.prologue_type
        }
    
    @classmethod
    def from_dict(cls, data: Dict) -> 'FunctionInfo':
        """Create FunctionInfo from dictionary."""
        return cls(
            offset=int(data["offset"], 16) if isinstance(data["offset"], str) else data["offset"],
            address=int(data["address"], 16) if isinstance(data["address"], str) else data["address"],
            name=data.get("name"),
            confidence=data.get("confidence", "low"),
            discovery_method=data.get("discovery_method", "static"),
            call_count=data.get("call_count", 0),
            callers=[int(c, 16) if isinstance(c, str) else c for c in data.get("callers", [])],
            callees=[int(c, 16) if isinstance(c, str) else c for c in data.get("callees", [])],
            signature=data.get("signature"),
            prologue_type=data.get("prologue_type")
        )


@dataclass
class CallGraphEdge:
    """A call graph edge."""
    caller: int
    callee: int
    call_type: str = "direct"  # "direct", "indirect", "virtual"
    
    def to_dict(self) -> Dict:
        """Convert to dictionary for JSON export."""
        return {
            "caller": f"0x{self.caller:X}",
            "callee": f"0x{self.callee:X}",
            "type": self.call_type
        }
    
    @classmethod
    def from_dict(cls, data: Dict) -> 'CallGraphEdge':
        """Create CallGraphEdge from dictionary."""
        return cls(
            caller=int(data["caller"], 16) if isinstance(data["caller"], str) else data["caller"],
            callee=int(data["callee"], 16) if isinstance(data["callee"], str) else data["callee"],
            call_type=data.get("type", "direct")
        )


@dataclass
class FunctionSignature:
    """Function signature information."""
    args: int
    arg_types: List[str]
    return_type: str
    confidence: float = 0.5
    
    def to_dict(self) -> Dict:
        """Convert to dictionary for JSON export."""
        return {
            "args": self.args,
            "arg_types": self.arg_types,
            "return_type": self.return_type,
            "confidence": self.confidence
        }
    
    @classmethod
    def from_dict(cls, data: Dict) -> 'FunctionSignature':
        """Create FunctionSignature from dictionary."""
        return cls(
            args=data.get("args", 0),
            arg_types=data.get("arg_types", []),
            return_type=data.get("return_type", "void"),
            confidence=data.get("confidence", 0.5)
        )
