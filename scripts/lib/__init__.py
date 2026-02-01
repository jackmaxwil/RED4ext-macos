"""
RED4ext Address Discovery Library

Tools for finding function addresses in Cyberpunk 2077 macOS binary.
"""

from .address_discovery import AddressDiscovery, FunctionInfo, StringRef

__all__ = ['AddressDiscovery', 'FunctionInfo', 'StringRef']
