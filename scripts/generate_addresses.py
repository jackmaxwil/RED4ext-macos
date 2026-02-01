#!/usr/bin/env python3
"""
Address Database Generator for RED4ext macOS Port

Optimized version with:
- Robust error handling with custom exceptions
- Performance: caching, mmap, batch processing
- Observability: structured logging, timing, metrics
- Concurrency: thread pools for subprocess calls
- Memory management: mmap, generators, chunked processing
- Safety: input validation, context managers, type checking

Usage:
    python3 generate_addresses.py <binary_path> [options]
    
Options:
    --symbols, -s    Path to cyberpunk2077_symbols.json
    --manual, -m     Path to manual_addresses.json
    --output, -o     Output JSON file path
    --game-version   Game version string
    --verbose, -v    Enable verbose output
    --parallel       Number of parallel workers (default: CPU count)
    --no-cache       Disable caching
"""

from __future__ import annotations

import argparse
import contextlib
import functools
import hashlib
import json
import logging
import mmap
import os
import re
import signal
import struct
import subprocess
import sys
import tempfile
import threading
import time
from concurrent.futures import ThreadPoolExecutor, as_completed
from dataclasses import dataclass, field
from enum import Enum
from pathlib import Path
from typing import (
    Any,
    Callable,
    Dict,
    Generator,
    Iterator,
    List,
    NamedTuple,
    Optional,
    Set,
    Tuple,
    TypeVar,
    Union,
)

# Type aliases
Address = int
HashValue = int
SegmentId = int
T = TypeVar('T')


# ============================================================================
# Custom Exceptions
# ============================================================================

class AddressGeneratorError(Exception):
    """Base exception for address generator errors."""
    pass


class BinaryNotFoundError(AddressGeneratorError):
    """Binary file not found or inaccessible."""
    pass


class BinaryParseError(AddressGeneratorError):
    """Failed to parse binary file."""
    pass


class HashConstantError(AddressGeneratorError):
    """Failed to load hash constants."""
    pass


class SymbolResolutionError(AddressGeneratorError):
    """Failed to resolve symbol address."""
    pass


class PatternScanError(AddressGeneratorError):
    """Pattern scan failed."""
    pass


class OutputError(AddressGeneratorError):
    """Failed to write output."""
    pass


# ============================================================================
# Data Classes and Enums
# ============================================================================

class SegmentType(Enum):
    """Mach-O segment types."""
    TEXT = 1
    DATA_CONST = 2
    DATA = 3
    UNKNOWN = 0


@dataclass
class Segment:
    """Mach-O segment information."""
    name: str
    vmaddr: int
    vmsize: int
    fileoff: int
    filesize: int
    segment_type: SegmentType = SegmentType.UNKNOWN


@dataclass
class AddressEntry:
    """Resolved address entry."""
    name: str
    hash_value: HashValue
    address: Optional[Address] = None
    segment: SegmentType = SegmentType.TEXT
    offset: int = 0
    source: str = "unknown"  # "manual", "symbol", "pattern", "unresolved"
    symbol_name: Optional[str] = None
    confidence: float = 1.0


@dataclass
class ResolutionStats:
    """Statistics for address resolution."""
    total: int = 0
    resolved_manual: int = 0
    resolved_symbol: int = 0
    resolved_pattern: int = 0
    unresolved: int = 0
    errors: int = 0
    elapsed_time: float = 0.0
    
    @property
    def resolved_total(self) -> int:
        return self.resolved_manual + self.resolved_symbol + self.resolved_pattern
    
    @property
    def success_rate(self) -> float:
        return self.resolved_total / self.total * 100 if self.total > 0 else 0.0


@dataclass
class Config:
    """Configuration for address generation."""
    binary_path: Path
    symbols_path: Optional[Path] = None
    manual_path: Optional[Path] = None
    output_path: Path = field(default_factory=lambda: Path('cyberpunk2077_addresses.json'))
    game_version: str = "2.3.1"
    parallel_workers: int = field(default_factory=lambda: min(os.cpu_count() or 4, 8))
    enable_cache: bool = True
    cache_dir: Path = field(default_factory=lambda: Path(tempfile.gettempdir()) / 'red4ext_cache')
    verbose: bool = False
    timeout: float = 30.0


# ============================================================================
# Logging Configuration
# ============================================================================

class ColoredFormatter(logging.Formatter):
    """Colored log formatter for terminal output."""
    
    COLORS = {
        'DEBUG': '\033[36m',    # Cyan
        'INFO': '\033[32m',     # Green
        'WARNING': '\033[33m',  # Yellow
        'ERROR': '\033[31m',    # Red
        'CRITICAL': '\033[35m', # Magenta
    }
    RESET = '\033[0m'
    
    def format(self, record: logging.LogRecord) -> str:
        color = self.COLORS.get(record.levelname, '')
        record.levelname = f"{color}{record.levelname}{self.RESET}"
        return super().format(record)


def setup_logging(verbose: bool = False) -> logging.Logger:
    """Configure logging with appropriate level and formatting."""
    logger = logging.getLogger('generate_addresses')
    logger.setLevel(logging.DEBUG if verbose else logging.INFO)
    
    # Clear existing handlers
    logger.handlers.clear()
    
    # Console handler with colors
    console = logging.StreamHandler(sys.stdout)
    console.setLevel(logging.DEBUG if verbose else logging.INFO)
    
    # Use colored formatter if terminal supports it
    if sys.stdout.isatty():
        formatter = ColoredFormatter(
            '%(asctime)s.%(msecs)03d │ %(levelname)-8s │ %(message)s',
            datefmt='%H:%M:%S'
        )
    else:
        formatter = logging.Formatter(
            '%(asctime)s │ %(levelname)-8s │ %(message)s',
            datefmt='%H:%M:%S'
        )
    
    console.setFormatter(formatter)
    logger.addHandler(console)
    
    return logger


# ============================================================================
# Utilities and Decorators
# ============================================================================

def timed(func: Callable[..., T]) -> Callable[..., T]:
    """Decorator to time function execution."""
    @functools.wraps(func)
    def wrapper(*args, **kwargs) -> T:
        start = time.perf_counter()
        try:
            return func(*args, **kwargs)
        finally:
            elapsed = time.perf_counter() - start
            logger = logging.getLogger('generate_addresses')
            logger.debug(f"⏱ {func.__name__} completed in {elapsed:.3f}s")
    return wrapper


def retry(max_attempts: int = 3, delay: float = 0.5, 
          exceptions: tuple = (subprocess.SubprocessError,)) -> Callable:
    """Decorator for retrying failed operations."""
    def decorator(func: Callable[..., T]) -> Callable[..., T]:
        @functools.wraps(func)
        def wrapper(*args, **kwargs) -> T:
            last_exception = None
            for attempt in range(max_attempts):
                try:
                    return func(*args, **kwargs)
                except exceptions as e:
                    last_exception = e
                    if attempt < max_attempts - 1:
                        time.sleep(delay * (attempt + 1))
            raise last_exception
        return wrapper
    return decorator


@contextlib.contextmanager
def mmap_binary(path: Path) -> Generator[mmap.mmap, None, None]:
    """Memory-map a binary file for efficient access."""
    with open(path, 'rb') as f:
        try:
            mm = mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_READ)
            yield mm
        finally:
            mm.close()


class Cache:
    """Thread-safe cache for expensive operations."""
    
    def __init__(self, cache_dir: Path, enabled: bool = True):
        self._cache_dir = cache_dir
        self._enabled = enabled
        self._memory_cache: Dict[str, Any] = {}
        self._lock = threading.Lock()
        
        if enabled:
            cache_dir.mkdir(parents=True, exist_ok=True)
    
    def _get_key(self, namespace: str, *args) -> str:
        """Generate cache key from arguments."""
        data = f"{namespace}:{':'.join(str(a) for a in args)}"
        return hashlib.md5(data.encode()).hexdigest()
    
    def get(self, namespace: str, *args) -> Optional[Any]:
        """Get cached value."""
        if not self._enabled:
            return None
        
        key = self._get_key(namespace, *args)
        
        # Check memory cache first
        with self._lock:
            if key in self._memory_cache:
                return self._memory_cache[key]
        
        # Check disk cache
        cache_file = self._cache_dir / f"{key}.json"
        if cache_file.exists():
            try:
                with open(cache_file, 'r') as f:
                    data = json.load(f)
                with self._lock:
                    self._memory_cache[key] = data
                return data
            except (json.JSONDecodeError, IOError):
                cache_file.unlink(missing_ok=True)
        
        return None
    
    def set(self, namespace: str, value: Any, *args) -> None:
        """Set cached value."""
        if not self._enabled:
            return
        
        key = self._get_key(namespace, *args)
        
        # Memory cache
        with self._lock:
            self._memory_cache[key] = value
        
        # Disk cache (for persistence)
        try:
            cache_file = self._cache_dir / f"{key}.json"
            with open(cache_file, 'w') as f:
                json.dump(value, f)
        except (IOError, TypeError):
            pass  # Non-critical - disk cache failure is okay


class ProgressTracker:
    """Thread-safe progress tracking."""
    
    def __init__(self, total: int, description: str = "Processing"):
        self._total = total
        self._current = 0
        self._description = description
        self._lock = threading.Lock()
        self._start_time = time.perf_counter()
        self._logger = logging.getLogger('generate_addresses')
    
    def update(self, n: int = 1, message: str = "") -> None:
        """Update progress counter."""
        with self._lock:
            self._current += n
            
            # Log progress at intervals
            if self._current % max(1, self._total // 10) == 0 or self._current == self._total:
                elapsed = time.perf_counter() - self._start_time
                rate = self._current / elapsed if elapsed > 0 else 0
                pct = self._current / self._total * 100
                self._logger.info(
                    f"📊 {self._description}: {self._current}/{self._total} "
                    f"({pct:.1f}%) - {rate:.1f}/s"
                    + (f" - {message}" if message else "")
                )
    
    @property
    def current(self) -> int:
        with self._lock:
            return self._current


# ============================================================================
# Binary Analysis
# ============================================================================

class MachOParser:
    """Parser for Mach-O binary files."""
    
    def __init__(self, binary_path: Path, cache: Optional[Cache] = None):
        self._path = binary_path
        self._cache = cache
        self._logger = logging.getLogger('generate_addresses')
        self._segments: Optional[Dict[str, Segment]] = None
        self._symbols: Optional[Dict[str, int]] = None
    
    def validate(self) -> None:
        """Validate binary file exists and is accessible."""
        if not self._path.exists():
            raise BinaryNotFoundError(f"Binary not found: {self._path}")
        
        if not self._path.is_file():
            raise BinaryNotFoundError(f"Path is not a file: {self._path}")
        
        if not os.access(self._path, os.R_OK):
            raise BinaryNotFoundError(f"Binary not readable: {self._path}")
        
        # Verify Mach-O magic number
        with open(self._path, 'rb') as f:
            magic = struct.unpack('<I', f.read(4))[0]
            if magic not in (0xFEEDFACF, 0xFEEDFACE, 0xBEBAFECA, 0xCAFEBABE):
                raise BinaryParseError(
                    f"Invalid Mach-O magic: 0x{magic:08X} "
                    "(expected 0xFEEDFACF for 64-bit)"
                )
    
    @timed
    def parse_segments(self) -> Dict[str, Segment]:
        """Parse Mach-O segments with caching."""
        if self._segments is not None:
            return self._segments
        
        # Check cache
        cache_key = str(self._path.stat().st_mtime)
        if self._cache:
            cached = self._cache.get('segments', cache_key)
            if cached:
                self._segments = {
                    name: Segment(**data) for name, data in cached.items()
                }
                self._logger.debug(f"Loaded {len(self._segments)} segments from cache")
                return self._segments
        
        self._logger.info("Parsing Mach-O segments...")
        
        try:
            result = subprocess.run(
                ['otool', '-l', str(self._path)],
                capture_output=True,
                text=True,
                check=True,
                timeout=30
            )
        except subprocess.CalledProcessError as e:
            raise BinaryParseError(f"otool failed: {e.stderr}")
        except subprocess.TimeoutExpired:
            raise BinaryParseError("otool timed out parsing segments")
        except FileNotFoundError:
            raise BinaryParseError("otool not found - install Xcode command line tools")
        
        segments = {}
        lines = result.stdout.split('\n')
        i = 0
        
        while i < len(lines):
            line = lines[i].strip()
            
            if 'cmd LC_SEGMENT_64' in line:
                seg_data = {'vmaddr': 0, 'vmsize': 0, 'fileoff': 0, 'filesize': 0}
                segname = None
                
                for j in range(1, 12):
                    if i + j >= len(lines):
                        break
                    next_line = lines[i + j].strip()
                    
                    if next_line.startswith('segname '):
                        segname = next_line.split()[1]
                    elif 'cmd ' in next_line:
                        break
                    else:
                        for key in ['vmaddr', 'vmsize', 'fileoff', 'filesize']:
                            if next_line.startswith(f'{key} '):
                                match = re.search(rf'{key}\s+0x([0-9a-fA-F]+)', next_line)
                                if match:
                                    seg_data[key] = int(match.group(1), 16)
                
                if segname and segname.startswith('__'):
                    seg_type = {
                        '__TEXT': SegmentType.TEXT,
                        '__DATA_CONST': SegmentType.DATA_CONST,
                        '__DATA': SegmentType.DATA,
                    }.get(segname, SegmentType.UNKNOWN)
                    
                    segments[segname] = Segment(
                        name=segname,
                        vmaddr=seg_data['vmaddr'],
                        vmsize=seg_data['vmsize'],
                        fileoff=seg_data['fileoff'],
                        filesize=seg_data['filesize'],
                        segment_type=seg_type
                    )
            i += 1
        
        self._segments = segments
        
        # Cache results
        if self._cache:
            cache_data = {
                name: {
                    'name': seg.name,
                    'vmaddr': seg.vmaddr,
                    'vmsize': seg.vmsize,
                    'fileoff': seg.fileoff,
                    'filesize': seg.filesize,
                    'segment_type': seg.segment_type.value
                }
                for name, seg in segments.items()
            }
            self._cache.set('segments', cache_data, cache_key)
        
        self._logger.info(f"Found {len(segments)} segments: {list(segments.keys())}")
        return segments
    
    @timed
    def load_symbols(self) -> Dict[str, int]:
        """Load all exported symbols with caching."""
        if self._symbols is not None:
            return self._symbols
        
        # Check cache
        cache_key = str(self._path.stat().st_mtime)
        if self._cache:
            cached = self._cache.get('symbols', cache_key)
            if cached:
                self._symbols = cached
                self._logger.debug(f"Loaded {len(self._symbols)} symbols from cache")
                return self._symbols
        
        self._logger.info("Loading symbols from binary...")
        
        try:
            result = subprocess.run(
                ['nm', '-g', '-n', str(self._path)],
                capture_output=True,
                text=True,
                check=True,
                timeout=120
            )
        except subprocess.CalledProcessError as e:
            self._logger.warning(f"nm failed: {e.stderr}")
            self._symbols = {}
            return self._symbols
        except subprocess.TimeoutExpired:
            self._logger.warning("nm timed out")
            self._symbols = {}
            return self._symbols
        except FileNotFoundError:
            raise BinaryParseError("nm not found - install Xcode command line tools")
        
        symbols = {}
        for line in result.stdout.split('\n'):
            parts = line.split()
            if len(parts) >= 3:
                try:
                    addr = int(parts[0], 16)
                    symbol = parts[2] if len(parts) > 2 else parts[1]
                    symbols[symbol] = addr
                except ValueError:
                    continue
        
        self._symbols = symbols
        
        # Cache results
        if self._cache:
            self._cache.set('symbols', symbols, cache_key)
        
        self._logger.info(f"Loaded {len(symbols)} symbols")
        return symbols
    
    def resolve_symbol(self, symbol_name: str) -> Optional[int]:
        """Resolve a symbol to its address."""
        symbols = self.load_symbols()
        return symbols.get(symbol_name)


class PatternScanner:
    """ARM64 pattern scanner with mmap support."""
    
    def __init__(self, binary_path: Path):
        self._path = binary_path
        self._logger = logging.getLogger('generate_addresses')
    
    def scan(self, pattern: str, segment: Optional[Segment] = None,
             max_matches: int = 100) -> List[int]:
        """
        Scan binary for byte pattern.
        
        Pattern format: "48 89 5C 24 ??" where ?? is wildcard byte.
        Returns list of file offsets.
        """
        pattern_bytes, mask = self._parse_pattern(pattern)
        if not pattern_bytes:
            return []
        
        matches = []
        
        with mmap_binary(self._path) as mm:
            start = segment.fileoff if segment else 0
            end = (segment.fileoff + segment.filesize) if segment else len(mm)
            
            i = start
            while i < end - len(pattern_bytes) + 1:
                if self._match_at(mm, i, pattern_bytes, mask):
                    matches.append(i)
                    if len(matches) >= max_matches:
                        break
                i += 1
        
        return matches
    
    def _parse_pattern(self, pattern: str) -> Tuple[bytes, List[bool]]:
        """Parse pattern string into bytes and mask."""
        pattern_bytes = []
        mask = []
        
        for part in pattern.split():
            if part in ('?', '??'):
                pattern_bytes.append(0)
                mask.append(False)
            else:
                try:
                    pattern_bytes.append(int(part, 16))
                    mask.append(True)
                except ValueError:
                    self._logger.warning(f"Invalid pattern byte: {part}")
        
        return bytes(pattern_bytes), mask
    
    def _match_at(self, data: Union[bytes, mmap.mmap], offset: int,
                   pattern: bytes, mask: List[bool]) -> bool:
        """Check if pattern matches at offset."""
        for i, (pb, m) in enumerate(zip(pattern, mask)):
            if m and data[offset + i] != pb:
                return False
        return True


# ============================================================================
# Symbol Resolution
# ============================================================================

class DemanglerPool:
    """Pool of c++filt processes for efficient demangling."""
    
    _instance: Optional['DemanglerPool'] = None
    _lock = threading.Lock()
    
    def __init__(self, pool_size: int = 4):
        self._pool_size = pool_size
        self._processes: List[subprocess.Popen] = []
        self._available: List[subprocess.Popen] = []
        self._process_lock = threading.Lock()
        self._logger = logging.getLogger('generate_addresses')
    
    @classmethod
    def get_instance(cls, pool_size: int = 4) -> 'DemanglerPool':
        """Get or create singleton instance."""
        with cls._lock:
            if cls._instance is None:
                cls._instance = cls(pool_size)
            return cls._instance
    
    def __enter__(self) -> 'DemanglerPool':
        self._start_pool()
        return self
    
    def __exit__(self, *args) -> None:
        self._stop_pool()
    
    def _start_pool(self) -> None:
        """Start c++filt processes."""
        for _ in range(self._pool_size):
            try:
                proc = subprocess.Popen(
                    ['c++filt'],
                    stdin=subprocess.PIPE,
                    stdout=subprocess.PIPE,
                    stderr=subprocess.DEVNULL,
                    text=True,
                    bufsize=1
                )
                self._processes.append(proc)
                self._available.append(proc)
            except FileNotFoundError:
                self._logger.warning("c++filt not found - demangling disabled")
                break
    
    def _stop_pool(self) -> None:
        """Stop all c++filt processes."""
        for proc in self._processes:
            try:
                proc.terminate()
                proc.wait(timeout=1)
            except (subprocess.TimeoutExpired, OSError):
                proc.kill()
        self._processes.clear()
        self._available.clear()
    
    def demangle(self, symbol: str, timeout: float = 1.0) -> Optional[str]:
        """Demangle a single symbol."""
        proc = None
        
        with self._process_lock:
            if self._available:
                proc = self._available.pop()
        
        if proc is None:
            return None
        
        try:
            proc.stdin.write(symbol + '\n')
            proc.stdin.flush()
            
            # Read with timeout using select
            import select
            ready, _, _ = select.select([proc.stdout], [], [], timeout)
            if ready:
                result = proc.stdout.readline().strip()
                with self._process_lock:
                    self._available.append(proc)
                return result if result != symbol else None
            else:
                # Timeout - kill and restart process
                proc.kill()
                new_proc = subprocess.Popen(
                    ['c++filt'],
                    stdin=subprocess.PIPE,
                    stdout=subprocess.PIPE,
                    stderr=subprocess.DEVNULL,
                    text=True,
                    bufsize=1
                )
                with self._process_lock:
                    self._processes.remove(proc)
                    self._processes.append(new_proc)
                    self._available.append(new_proc)
                return None
        except (BrokenPipeError, OSError):
            # Process died - restart
            try:
                new_proc = subprocess.Popen(
                    ['c++filt'],
                    stdin=subprocess.PIPE,
                    stdout=subprocess.PIPE,
                    stderr=subprocess.DEVNULL,
                    text=True,
                    bufsize=1
                )
                with self._process_lock:
                    if proc in self._processes:
                        self._processes.remove(proc)
                    self._processes.append(new_proc)
                    self._available.append(new_proc)
            except FileNotFoundError:
                pass
            return None
    
    def demangle_batch(self, symbols: List[str]) -> Dict[str, str]:
        """Demangle multiple symbols efficiently."""
        results = {}
        
        with ThreadPoolExecutor(max_workers=self._pool_size) as executor:
            futures = {
                executor.submit(self.demangle, sym): sym
                for sym in symbols
            }
            
            for future in as_completed(futures):
                symbol = futures[future]
                try:
                    demangled = future.result()
                    if demangled:
                        results[symbol] = demangled
                except Exception:
                    pass
        
        return results


class SymbolResolver:
    """Resolves symbols using multiple strategies."""
    
    def __init__(self, macho: MachOParser, cache: Optional[Cache] = None):
        self._macho = macho
        self._cache = cache
        self._logger = logging.getLogger('generate_addresses')
        self._name_to_symbol: Dict[str, str] = {}
        self._hash_to_symbol: Dict[int, str] = {}
        self._demangled_cache: Dict[str, str] = {}
    
    @timed
    def load_symbol_mappings(self, symbols_json: Path) -> None:
        """Load symbol mappings from JSON file."""
        if not symbols_json.exists():
            self._logger.warning(f"Symbol file not found: {symbols_json}")
            return
        
        # Check cache
        cache_key = str(symbols_json.stat().st_mtime)
        if self._cache:
            cached = self._cache.get('symbol_mappings', cache_key)
            if cached:
                self._name_to_symbol = cached.get('name_to_symbol', {})
                self._hash_to_symbol = {int(k): v for k, v in cached.get('hash_to_symbol', {}).items()}
                self._logger.debug("Loaded symbol mappings from cache")
                return
        
        self._logger.info(f"Loading symbol mappings from {symbols_json}...")
        
        try:
            with open(symbols_json, 'r') as f:
                data = json.load(f)
        except (json.JSONDecodeError, IOError) as e:
            raise SymbolResolutionError(f"Failed to load symbols: {e}")
        
        mappings = data.get('mappings', [])
        cpp_symbols = [e for e in mappings if e.get('symbol', '').startswith('__Z')]
        
        self._logger.info(f"Processing {len(cpp_symbols)} C++ symbols...")
        
        # Batch demangle for efficiency
        symbols_to_demangle = [e.get('symbol', '') for e in cpp_symbols[:5000]]
        
        with DemanglerPool.get_instance() as pool:
            self._demangled_cache = pool.demangle_batch(symbols_to_demangle)
        
        progress = ProgressTracker(len(cpp_symbols), "Symbol processing")
        
        for entry in cpp_symbols:
            hash_str = entry.get('hash', '')
            symbol = entry.get('symbol', '')
            
            if hash_str.startswith('0x') and symbol:
                hash_value = int(hash_str, 16)
                self._hash_to_symbol[hash_value] = symbol
            
            demangled = self._demangled_cache.get(symbol)
            if demangled:
                # Extract class and method names
                match = re.search(r'(\w+)::(\w+)\s*\(', demangled)
                if match:
                    canonical = f"{match.group(1)}_{match.group(2)}"
                    self._name_to_symbol[canonical] = symbol
                
                # Non-member functions
                match = re.search(r'^(\w+)\s*\(', demangled)
                if match:
                    self._name_to_symbol[match.group(1)] = symbol
            
            progress.update()
        
        # Cache results
        if self._cache:
            self._cache.set('symbol_mappings', {
                'name_to_symbol': self._name_to_symbol,
                'hash_to_symbol': {str(k): v for k, v in self._hash_to_symbol.items()}
            }, cache_key)
        
        self._logger.info(
            f"Loaded {len(self._hash_to_symbol)} hash mappings, "
            f"{len(self._name_to_symbol)} name mappings"
        )
    
    def resolve(self, name: str, hash_value: int) -> Optional[Tuple[int, str]]:
        """
        Resolve an address by name and hash.
        Returns (address, symbol_name) or None.
        """
        # Try hash-based lookup
        symbol = self._hash_to_symbol.get(hash_value)
        
        # Try name-based lookup
        if not symbol:
            symbol = self._name_to_symbol.get(name)
        
        # Try variations
        if not symbol and '_' in name:
            parts = name.split('_', 1)
            if len(parts) == 2:
                symbol = self._name_to_symbol.get(f"{parts[0]}_{parts[1]}")
        
        if symbol:
            addr = self._macho.resolve_symbol(symbol)
            if addr:
                return (addr, symbol)
        
        return None


# ============================================================================
# Hash Constant Loading
# ============================================================================

class HashConstantLoader:
    """Loads hash constants from AddressHashes.hpp files."""
    
    HASH_PATTERN = re.compile(
        r'constexpr\s+std::uint32_t\s+(\w+)\s*=\s*(0x[0-9A-Fa-f]+|\d+)[UuLl]*\s*;'
    )
    
    def __init__(self, scripts_dir: Path):
        self._scripts_dir = scripts_dir
        self._logger = logging.getLogger('generate_addresses')
    
    @timed
    def load(self) -> Dict[str, int]:
        """Load hash constants from all known locations."""
        hash_files = [
            # SDK
            self._scripts_dir.parent.parent / 'RED4ext.SDK' / 'include' / 'RED4ext' / 'Detail' / 'AddressHashes.hpp',
            # Local
            self._scripts_dir.parent / 'src' / 'dll' / 'Detail' / 'AddressHashes.hpp',
            # Deps fallback
            self._scripts_dir.parent / 'deps' / 'red4ext.sdk' / 'include' / 'RED4ext' / 'Detail' / 'AddressHashes.hpp',
        ]
        
        hashes: Dict[str, int] = {}
        loaded_files = 0
        
        for hash_file in hash_files:
            if not hash_file.exists():
                continue
            
            self._logger.debug(f"Loading hashes from {hash_file}")
            
            try:
                content = hash_file.read_text()
                
                for match in self.HASH_PATTERN.finditer(content):
                    name = match.group(1)
                    value_str = match.group(2)
                    
                    if value_str.startswith('0x'):
                        value = int(value_str, 16)
                    else:
                        value = int(value_str)
                    
                    hashes[name] = value & 0xFFFFFFFF
                
                loaded_files += 1
                
            except IOError as e:
                self._logger.warning(f"Failed to read {hash_file}: {e}")
        
        if loaded_files == 0:
            raise HashConstantError(
                "No AddressHashes.hpp files found. "
                "Checked: SDK, local src/dll, deps"
            )
        
        self._logger.info(f"Loaded {len(hashes)} hash constants from {loaded_files} file(s)")
        return hashes


# ============================================================================
# Manual Address Loading
# ============================================================================

class ManualAddressLoader:
    """Loads manually specified addresses."""
    
    def __init__(self):
        self._logger = logging.getLogger('generate_addresses')
    
    def load(self, path: Path) -> Dict[str, Dict[str, Any]]:
        """Load manual address mappings from JSON."""
        if not path.exists():
            return {}
        
        try:
            with open(path, 'r') as f:
                data = json.load(f)
        except (json.JSONDecodeError, IOError) as e:
            self._logger.warning(f"Failed to load manual addresses: {e}")
            return {}
        
        manual = {}
        entries = data.get('addresses', [])
        
        for entry in entries:
            name = entry.get('name', '')
            addr_str = entry.get('address', '')
            segment = entry.get('segment', 1)
            
            if name and addr_str.startswith('0x'):
                try:
                    addr = int(addr_str, 16)
                    manual[name] = {'address': addr, 'segment': segment}
                except ValueError:
                    self._logger.warning(f"Invalid address for {name}: {addr_str}")
        
        self._logger.info(f"Loaded {len(manual)} manual addresses")
        return manual


# ============================================================================
# Address Generator
# ============================================================================

class AddressGenerator:
    """Main address generation orchestrator."""
    
    def __init__(self, config: Config):
        self._config = config
        self._logger = setup_logging(config.verbose)
        self._cache = Cache(config.cache_dir, config.enable_cache)
        self._stats = ResolutionStats()
        self._shutdown = threading.Event()
        
        # Setup signal handlers
        signal.signal(signal.SIGINT, self._handle_signal)
        signal.signal(signal.SIGTERM, self._handle_signal)
    
    def _handle_signal(self, signum: int, frame: Any) -> None:
        """Handle shutdown signals gracefully."""
        self._logger.warning(f"Received signal {signum}, shutting down...")
        self._shutdown.set()
    
    @timed
    def generate(self) -> int:
        """
        Generate address database.
        Returns exit code (0 = success, 1 = error).
        """
        start_time = time.perf_counter()
        
        try:
            self._logger.info("=" * 60)
            self._logger.info("RED4ext Address Generator - macOS Port")
            self._logger.info("=" * 60)
            self._logger.info(f"Binary: {self._config.binary_path}")
            self._logger.info(f"Output: {self._config.output_path}")
            self._logger.info(f"Workers: {self._config.parallel_workers}")
            self._logger.info(f"Cache: {'enabled' if self._config.enable_cache else 'disabled'}")
            self._logger.info("-" * 60)
            
            # Initialize components
            macho = MachOParser(self._config.binary_path, self._cache)
            macho.validate()
            
            segments = macho.parse_segments()
            if not segments:
                raise BinaryParseError("No segments found in binary")
            
            # Load hash constants
            hash_loader = HashConstantLoader(Path(__file__).parent)
            hash_constants = hash_loader.load()
            self._stats.total = len(hash_constants)
            
            # Load manual addresses
            manual_addresses: Dict[str, Dict] = {}
            if self._config.manual_path:
                manual_loader = ManualAddressLoader()
                manual_addresses = manual_loader.load(self._config.manual_path)
            
            # Load symbol mappings
            resolver = SymbolResolver(macho, self._cache)
            if self._config.symbols_path:
                resolver.load_symbol_mappings(self._config.symbols_path)
            
            # Resolve addresses
            self._logger.info("-" * 60)
            self._logger.info("Resolving addresses...")
            
            entries = self._resolve_addresses(
                hash_constants,
                manual_addresses,
                resolver,
                segments
            )
            
            # Check for shutdown
            if self._shutdown.is_set():
                self._logger.warning("Shutdown requested, saving partial results...")
            
            # Write output
            self._write_output(entries, segments)
            
            # Report statistics
            self._stats.elapsed_time = time.perf_counter() - start_time
            self._report_stats()
            
            return 0 if self._stats.unresolved == 0 else 0  # Still success if some unresolved
            
        except AddressGeneratorError as e:
            self._logger.error(f"Generation failed: {e}")
            return 1
        except Exception as e:
            self._logger.exception(f"Unexpected error: {e}")
            return 1
    
    def _resolve_addresses(
        self,
        hash_constants: Dict[str, int],
        manual_addresses: Dict[str, Dict],
        resolver: SymbolResolver,
        segments: Dict[str, Segment]
    ) -> List[AddressEntry]:
        """Resolve all addresses using multiple strategies."""
        entries: List[AddressEntry] = []
        
        progress = ProgressTracker(len(hash_constants), "Address resolution")
        
        # Use thread pool for parallel resolution
        with ThreadPoolExecutor(max_workers=self._config.parallel_workers) as executor:
            futures = {}
            
            for name, hash_value in hash_constants.items():
                if self._shutdown.is_set():
                    break
                
                future = executor.submit(
                    self._resolve_single,
                    name,
                    hash_value,
                    manual_addresses,
                    resolver,
                    segments
                )
                futures[future] = name
            
            for future in as_completed(futures):
                if self._shutdown.is_set():
                    break
                
                name = futures[future]
                try:
                    entry = future.result(timeout=self._config.timeout)
                    entries.append(entry)
                    
                    # Update stats
                    if entry.source == "manual":
                        self._stats.resolved_manual += 1
                    elif entry.source == "symbol":
                        self._stats.resolved_symbol += 1
                    elif entry.source == "pattern":
                        self._stats.resolved_pattern += 1
                    else:
                        self._stats.unresolved += 1
                    
                except Exception as e:
                    self._logger.error(f"Failed to resolve {name}: {e}")
                    self._stats.errors += 1
                
                progress.update()
        
        return entries
    
    def _resolve_single(
        self,
        name: str,
        hash_value: int,
        manual_addresses: Dict[str, Dict],
        resolver: SymbolResolver,
        segments: Dict[str, Segment]
    ) -> AddressEntry:
        """Resolve a single address."""
        entry = AddressEntry(name=name, hash_value=hash_value)
        
        # Strategy 1: Manual address (highest priority)
        if name in manual_addresses:
            manual = manual_addresses[name]
            entry.address = manual['address']
            entry.segment = SegmentType(manual['segment'])
            entry.source = "manual"
            entry.offset = self._calculate_offset(entry.address, entry.segment, segments)
            self._logger.debug(f"✓ {name}: manual -> 0x{entry.address:X}")
            return entry
        
        # Strategy 2: Symbol resolution
        result = resolver.resolve(name, hash_value)
        if result:
            addr, symbol = result
            entry.address = addr
            entry.symbol_name = symbol
            entry.source = "symbol"
            entry.segment = self._determine_segment(addr, segments)
            entry.offset = self._calculate_offset(addr, entry.segment, segments)
            self._logger.debug(f"✓ {name}: symbol -> 0x{addr:X}")
            return entry
        
        # Strategy 3: Pattern scanning (TODO: implement ARM64 patterns)
        # entry = self._try_pattern_scan(entry, ...)
        
        # Not resolved
        entry.source = "unresolved"
        self._logger.debug(f"✗ {name}: unresolved (hash 0x{hash_value:08X})")
        return entry
    
    def _determine_segment(self, address: int, segments: Dict[str, Segment]) -> SegmentType:
        """Determine which segment an address belongs to."""
        for seg in segments.values():
            if seg.vmaddr <= address < seg.vmaddr + seg.vmsize:
                return seg.segment_type
        return SegmentType.TEXT
    
    def _calculate_offset(
        self,
        address: int,
        segment_type: SegmentType,
        segments: Dict[str, Segment]
    ) -> int:
        """Calculate offset relative to segment start."""
        seg_name = {
            SegmentType.TEXT: '__TEXT',
            SegmentType.DATA_CONST: '__DATA_CONST',
            SegmentType.DATA: '__DATA',
        }.get(segment_type, '__TEXT')
        
        seg = segments.get(seg_name)
        if seg:
            return address - seg.vmaddr
        return address
    
    def _write_output(
        self,
        entries: List[AddressEntry],
        segments: Dict[str, Segment]
    ) -> None:
        """Write output JSON file."""
        self._logger.info(f"Writing output to {self._config.output_path}...")
        
        addresses = []
        for entry in entries:
            if entry.source != "unresolved":
                addresses.append({
                    "hash": str(entry.hash_value),
                    "offset": f"{entry.segment.value}:0x{entry.offset:X}"
                })
        
        output_data = {
            "version": "1.0",
            "game_version": self._config.game_version,
            "generated": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
            "stats": {
                "total": self._stats.total,
                "resolved": self._stats.resolved_total,
                "unresolved": self._stats.unresolved
            },
            "Addresses": addresses
        }
        
        try:
            # Atomic write
            tmp_path = self._config.output_path.with_suffix('.tmp')
            with open(tmp_path, 'w') as f:
                json.dump(output_data, f, indent=2)
            tmp_path.rename(self._config.output_path)
        except IOError as e:
            raise OutputError(f"Failed to write output: {e}")
    
    def _report_stats(self) -> None:
        """Report generation statistics."""
        self._logger.info("=" * 60)
        self._logger.info("Generation Complete")
        self._logger.info("=" * 60)
        self._logger.info(f"Total addresses:     {self._stats.total}")
        self._logger.info(f"Resolved (manual):   {self._stats.resolved_manual}")
        self._logger.info(f"Resolved (symbol):   {self._stats.resolved_symbol}")
        self._logger.info(f"Resolved (pattern):  {self._stats.resolved_pattern}")
        self._logger.info(f"Unresolved:          {self._stats.unresolved}")
        self._logger.info(f"Errors:              {self._stats.errors}")
        self._logger.info(f"Success rate:        {self._stats.success_rate:.1f}%")
        self._logger.info(f"Elapsed time:        {self._stats.elapsed_time:.2f}s")
        self._logger.info("=" * 60)
        
        if self._stats.unresolved > 0:
            self._logger.warning(
                f"{self._stats.unresolved} addresses need pattern scanning or manual input"
            )


# ============================================================================
# Main Entry Point
# ============================================================================

def main() -> int:
    """Main entry point."""
    parser = argparse.ArgumentParser(
        description='Generate RED4ext address database from macOS binary',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
    # Basic usage
    python3 generate_addresses.py /path/to/Cyberpunk2077
    
    # With symbol mapping
    python3 generate_addresses.py /path/to/Cyberpunk2077 -s symbols.json
    
    # With manual addresses and verbose output
    python3 generate_addresses.py /path/to/Cyberpunk2077 -m manual.json -v
    
    # Disable caching for fresh scan
    python3 generate_addresses.py /path/to/Cyberpunk2077 --no-cache
        """
    )
    
    parser.add_argument(
        'binary',
        type=Path,
        help='Path to the Mach-O binary (game executable)'
    )
    parser.add_argument(
        '--symbols', '-s',
        type=Path,
        help='Path to cyberpunk2077_symbols.json'
    )
    parser.add_argument(
        '--manual', '-m',
        type=Path,
        help='Path to manual_addresses.json'
    )
    parser.add_argument(
        '--output', '-o',
        type=Path,
        default=Path('outputs/cyberpunk2077_addresses.json'),
        help='Output JSON file path (default: cyberpunk2077_addresses.json)'
    )
    parser.add_argument(
        '--game-version',
        type=str,
        default='2.3.1',
        help='Game version string (default: 2.3.1)'
    )
    parser.add_argument(
        '--verbose', '-v',
        action='store_true',
        help='Enable verbose output'
    )
    parser.add_argument(
        '--parallel', '-p',
        type=int,
        default=min(os.cpu_count() or 4, 8),
        help=f'Number of parallel workers (default: {min(os.cpu_count() or 4, 8)})'
    )
    parser.add_argument(
        '--no-cache',
        action='store_true',
        help='Disable caching'
    )
    
    args = parser.parse_args()
    
    # Validate binary path early
    if not args.binary.exists():
        print(f"Error: Binary not found: {args.binary}", file=sys.stderr)
        return 1
    
    config = Config(
        binary_path=args.binary,
        symbols_path=args.symbols,
        manual_path=args.manual,
        output_path=args.output,
        game_version=args.game_version,
        parallel_workers=args.parallel,
        enable_cache=not args.no_cache,
        verbose=args.verbose
    )
    
    generator = AddressGenerator(config)
    return generator.generate()


if __name__ == '__main__':
    sys.exit(main())
