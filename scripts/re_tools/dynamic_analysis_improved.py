#!/usr/bin/env python3
"""
Dynamic Analysis Script - Function Discovery via frida-python

Improved version with:
- Enhanced error handling and validation
- Structured logging with levels
- Progress tracking and observability
- Improved JSON save file format
- Frida JS best practices for macOS ARM64

Usage:
    python3 dynamic_analysis.py \
      --process-name "Cyberpunk2077" \
      --output dynamic_functions.json \
      --trace-duration 30 \
      --log-level INFO
"""

import json
import argparse
import sys
import time
import logging
from pathlib import Path
from typing import Dict, List, Tuple, Optional, Any
from datetime import datetime
from dataclasses import dataclass, asdict
from enum import Enum

# Import frida-python library
try:
    import frida
    FRIDA_AVAILABLE = True
except ImportError:
    FRIDA_AVAILABLE = False

# Import shared data structures
try:
    from re_common import FunctionInfo, CallGraphEdge, IMAGE_BASE
except ImportError:
    sys.path.insert(0, str(Path(__file__).parent))
    from re_common import FunctionInfo, CallGraphEdge, IMAGE_BASE


# ============================================================================
# Logging Setup
# ============================================================================

class LogLevel(Enum):
    """Log levels matching Python logging."""
    DEBUG = logging.DEBUG
    INFO = logging.INFO
    WARNING = logging.WARNING
    ERROR = logging.ERROR
    CRITICAL = logging.CRITICAL


def setup_logging(log_level: str = "INFO", log_file: Optional[Path] = None) -> logging.Logger:
    """Setup structured logging with file and console handlers."""
    level = getattr(logging, log_level.upper(), logging.INFO)
    
    logger = logging.getLogger("dynamic_analysis")
    logger.setLevel(level)
    logger.handlers.clear()  # Remove existing handlers
    
    # Format: timestamp level message
    formatter = logging.Formatter(
        '%(asctime)s [%(levelname)-8s] %(message)s',
        datefmt='%Y-%m-%d %H:%M:%S'
    )
    
    # Console handler
    console_handler = logging.StreamHandler(sys.stdout)
    console_handler.setLevel(level)
    console_handler.setFormatter(formatter)
    logger.addHandler(console_handler)
    
    # File handler (if specified)
    if log_file:
        file_handler = logging.FileHandler(log_file, mode='w')
        file_handler.setLevel(logging.DEBUG)  # Always log everything to file
        file_formatter = logging.Formatter(
            '%(asctime)s [%(levelname)-8s] [%(filename)s:%(lineno)d] %(message)s',
            datefmt='%Y-%m-%d %H:%M:%S'
        )
        file_handler.setFormatter(file_formatter)
        logger.addHandler(file_handler)
    
    return logger


# ============================================================================
# Progress Tracking
# ============================================================================

@dataclass
class ProgressMetrics:
    """Progress tracking metrics."""
    start_time: float
    elapsed_time: float = 0.0
    functions_hooked: int = 0
    functions_called: int = 0
    total_calls: int = 0
    call_graph_edges: int = 0
    errors: int = 0
    warnings: int = 0
    
    def update(self, **kwargs):
        """Update metrics."""
        for key, value in kwargs.items():
            if hasattr(self, key):
                setattr(self, key, value)
    
    def to_dict(self) -> Dict[str, Any]:
        """Convert to dictionary."""
        return {
            "elapsed_time_seconds": round(self.elapsed_time, 2),
            "functions_hooked": self.functions_hooked,
            "functions_called": self.functions_called,
            "total_calls": self.total_calls,
            "call_graph_edges": self.call_graph_edges,
            "errors": self.errors,
            "warnings": self.warnings,
            "calls_per_second": round(self.total_calls / self.elapsed_time, 2) if self.elapsed_time > 0 else 0
        }


# ============================================================================
# Frida JavaScript Code (Best Practices)
# ============================================================================

FRIDA_DISCOVERY_SCRIPT_TEMPLATE = """
'use strict';

// ============================================================================
// Configuration & Constants
// ============================================================================

const CONFIG = {{
    SAMPLE_RATE: {SAMPLE_RATE},
    MAX_CALLERS: 1000,  // Limit callers per function to prevent memory issues
    LOG_LEVEL: '{LOG_LEVEL}'
}};

// ============================================================================
// Logging Utilities (Best Practice: Structured logging)
// ============================================================================

function log(level, message, data) {{
    const timestamp = new Date().toISOString();
    const logEntry = {{
        timestamp: timestamp,
        level: level,
        message: message,
        data: data || {{}}
    }};
    
    if (level === 'ERROR' || level === 'WARN') {{
        console.error(`[${{timestamp}}] [${{level}}] ${{message}}`, data || '');
    }} else if (CONFIG.LOG_LEVEL === 'DEBUG') {{
        console.log(`[${{timestamp}}] [${{level}}] ${{message}}`, data || '');
    }}
    
    // Send to Python via RPC for structured logging
    try {{
        if (typeof rpc !== 'undefined' && rpc.exports && rpc.exports.log) {{
            rpc.exports.log(level, message, data);
        }}
    }} catch (e) {{
        // Ignore if RPC not available
    }}
}}

function logError(message, error) {{
    log('ERROR', message, {{
        error: error.toString(),
        stack: error.stack
    }});
}}

function logWarn(message, data) {{
    log('WARN', message, data);
}}

function logInfo(message, data) {{
    log('INFO', message, data);
}}

function logDebug(message, data) {{
    log('DEBUG', message, data);
}}

// ============================================================================
// Module Discovery (Best Practice: Robust base address finding)
// ============================================================================

function findCyberpunkBase() {{
    try {{
        // Method 1: Use Module.findBaseAddress if available
        if (typeof Module !== 'undefined' && Module && typeof Module.findBaseAddress === 'function') {{
            const base = Module.findBaseAddress('Cyberpunk2077');
            if (base && !base.isNull()) {{
                logInfo('Found module via Module.findBaseAddress', {{ base: base.toString() }});
                return base;
            }}
        }}
    }} catch (e) {{
        logWarn('Module.findBaseAddress failed', {{ error: e.toString() }});
    }}
    
    try {{
        // Method 2: Enumerate modules and find by name/path
        const mods = Process.enumerateModules();
        for (const m of mods) {{
            if (m.name === 'Cyberpunk2077') {{
                logInfo('Found module via enumeration (name)', {{ base: m.base.toString(), path: m.path }});
                return m.base;
            }}
            if (m.path && (
                m.path.endsWith('/Cyberpunk2077') || 
                m.path.endsWith('Cyberpunk2077.app/Contents/MacOS/Cyberpunk2077')
            )) {{
                logInfo('Found module via enumeration (path)', {{ base: m.base.toString(), path: m.path }});
                return m.base;
            }}
        }}
        
        // Fallback: first module is usually the main executable
        if (mods.length > 0) {{
            logWarn('Using fallback: first module', {{ base: mods[0].base.toString(), name: mods[0].name }});
            return mods[0].base;
        }}
    }} catch (e) {{
        logError('Module enumeration failed', e);
    }}
    
    return null;
}}

// ============================================================================
// Data Structures
// ============================================================================

const discoveredFunctions = new Map();
const callGraph = new Map();
const hookStats = {{
    totalHooks: 0,
    successfulHooks: 0,
    failedHooks: 0,
    hookErrors: []
}};

const moduleBase = findCyberpunkBase();

if (!moduleBase || moduleBase.isNull()) {{
    logError('Could not find Cyberpunk2077 module', {{}});
    rpc.exports = {{
        getFunctions: () => [],
        getCallGraph: () => ({{}}),
        getStats: () => ({{functionCount: 0, callGraphSize: 0, totalCalls: 0}}),
        getHookStats: () => hookStats,
        hookFunction: (offset) => {{ logError('Module not found, cannot hook', {{}}); return false; }},
        unhookFunction: (offset) => false,
        unhookAll: () => 0,
        log: (level, message, data) => {{}} // No-op if module not found
    }};
}} else {{
    logInfo('Module base address found', {{ base: moduleBase.toString() }});
    
    // ============================================================================
    // Hook Function (Best Practice: Error handling, validation, sampling)
    // ============================================================================
    
    function hookFunctionAtOffset(offset) {{
        try {{
            // Validation: Check offset is valid
            if (typeof offset !== 'number' || offset < 0 || offset >= 0x10000000) {{
                logWarn('Invalid offset provided', {{ offset: offset }});
                hookStats.failedHooks++;
                return false;
            }}
            
            const funcAddr = moduleBase.add(offset);
            const offsetKey = offset.toString();
            
            // Skip if already hooked
            if (discoveredFunctions.has(offsetKey)) {{
                logDebug('Function already hooked', {{ offset: offset }});
                return true;
            }}
            
            // Validate address is readable
            try {{
                funcAddr.readU8();  // Try to read to validate address
            }} catch (e) {{
                logWarn('Invalid function address', {{ offset: offset, error: e.toString() }});
                hookStats.failedHooks++;
                return false;
            }}
            
            // Initialize function info
            const callersSet = new Set();  // Use Set for O(1) uniqueness checks
            let callCount = 0;
            
            discoveredFunctions.set(offsetKey, {{
                offset: offset,
                address: funcAddr.toString(),
                callCount: 0,
                callers: [],
                callees: [],
                firstCallTime: null,
                lastCallTime: null
            }});
            
            // Hook this specific function address
            // OPTIMIZED: Minimize JavaScript execution to prevent freezes
            try {{
                Interceptor.attach(funcAddr, {{
                    onEnter: function(args) {{
                        try {{
                            callCount++;
                            const func = discoveredFunctions.get(offsetKey);
                            if (!func) return;
                            
                            const now = Date.now();
                            
                            // OPTIMIZATION: Only do expensive operations every Nth call
                            if (callCount % CONFIG.SAMPLE_RATE === 0) {{
                                func.callCount += CONFIG.SAMPLE_RATE;  // Batch increment
                                
                                // Track timing
                                if (!func.firstCallTime) {{
                                    func.firstCallTime = now;
                                }}
                                func.lastCallTime = now;
                                
                                // Track caller (sampled) - expensive operations
                                try {{
                                    const caller = this.returnAddress;
                                    if (caller && !caller.isNull() && caller.compare(moduleBase) >= 0) {{
                                        const callerOffset = caller.sub(moduleBase).toInt32();
                                        if (callerOffset > 0 && callerOffset < 0x10000000) {{
                                            // Use Set for O(1) check instead of indexOf O(n)
                                            if (!callersSet.has(callerOffset) && func.callers.length < CONFIG.MAX_CALLERS) {{
                                                callersSet.add(callerOffset);
                                                func.callers.push(callerOffset);
                                                
                                                // Update call graph (batched)
                                                const callerKey = callerOffset.toString();
                                                if (!callGraph.has(callerKey)) {{
                                                    callGraph.set(callerKey, []);
                                                }}
                                                const callees = callGraph.get(callerKey);
                                                if (callees.indexOf(offset) === -1) {{
                                                    callees.push(offset);
                                                }}
                                            }}
                                        }}
                                    }}
                                }} catch (e) {{
                                    // Silently ignore caller tracking errors
                                    logDebug('Caller tracking error', {{ error: e.toString() }});
                                }}
                            }} else {{
                                // Fast path: just increment counter (no expensive operations)
                                func.callCount++;
                            }}
                        }} catch (e) {{
                            // Silently ignore errors to avoid crashing
                            logDebug('Hook callback error', {{ error: e.toString() }});
                        }}
                    }},
                    onLeave: function(retval) {{
                        // Empty - no work on return to minimize overhead
                    }}
                }});
                
                hookStats.successfulHooks++;
                hookStats.totalHooks++;
                logDebug('Function hooked successfully', {{ offset: offset, address: funcAddr.toString() }});
                return true;
            }} catch (e) {{
                logError('Interceptor.attach failed', e);
                hookStats.failedHooks++;
                hookStats.hookErrors.push({{
                    offset: offset,
                    error: e.toString(),
                    stack: e.stack
                }});
                discoveredFunctions.delete(offsetKey);  // Cleanup
                return false;
            }}
        }} catch (e) {{
            logError('hookFunctionAtOffset failed', e);
            hookStats.failedHooks++;
            return false;
        }}
    }}
    
    // ============================================================================
    // RPC Exports (Best Practice: Structured data, error handling)
    // ============================================================================
    
    rpc.exports = {{
        getFunctions: function() {{
            try {{
                return Array.from(discoveredFunctions.values());
            }} catch (e) {{
                logError('getFunctions failed', e);
                return [];
            }}
        }},
        
        getCallGraph: function() {{
            try {{
                const result = {{}};
                for (const [caller, callees] of callGraph.entries()) {{
                    result[caller] = callees;
                }}
                return result;
            }} catch (e) {{
                logError('getCallGraph failed', e);
                return {{}};
            }}
        }},
        
        getStats: function() {{
            try {{
                let totalCalls = 0;
                for (const func of discoveredFunctions.values()) {{
                    totalCalls += func.callCount;
                }}
                return {{
                    functionCount: discoveredFunctions.size,
                    callGraphSize: callGraph.size,
                    totalCalls: totalCalls,
                    hookStats: hookStats
                }};
            }} catch (e) {{
                logError('getStats failed', e);
                return {{functionCount: 0, callGraphSize: 0, totalCalls: 0}};
            }}
        }},
        
        getHookStats: function() {{
            return hookStats;
        }},
        
        hookFunction: hookFunctionAtOffset,
        
        unhookFunction: function(offset) {{
            try {{
                const offsetKey = offset.toString();
                if (discoveredFunctions.has(offsetKey)) {{
                    discoveredFunctions.delete(offsetKey);
                    logDebug('Function unhooked', {{ offset: offset }});
                    return true;
                }}
                return false;
            }} catch (e) {{
                logError('unhookFunction failed', e);
                return false;
            }}
        }},
        
        unhookAll: function() {{
            try {{
                const count = discoveredFunctions.size;
                discoveredFunctions.clear();
                callGraph.clear();
                logInfo('All functions unhooked', {{ count: count }});
                return count;
            }} catch (e) {{
                logError('unhookAll failed', e);
                return 0;
            }}
        }},
        
        log: function(level, message, data) {{
            // RPC log handler (called from JavaScript)
            console.log(`[RPC] [${{level}}] ${{message}}`, data || '');
        }}
    }};
    
    logInfo('Frida script initialized successfully', {{}});
}}
"""


# ============================================================================
# Main Functions
# ============================================================================

def find_process(process_name: str, logger: logging.Logger) -> Optional[int]:
    """Find process ID by name (flexible matching with error handling)."""
    if not FRIDA_AVAILABLE:
        logger.error("frida-python not available")
        return None
    
    try:
        device = frida.get_local_device()
        processes = device.enumerate_processes()
        
        # Try exact match first
        for proc in processes:
            if proc.name.lower() == process_name.lower():
                logger.info(f"Found process (exact): {proc.name} (PID: {proc.pid})")
                return proc.pid
        
        # Try substring match
        for proc in processes:
            if process_name.lower() in proc.name.lower():
                logger.info(f"Found process (substring): {proc.name} (PID: {proc.pid})")
                return proc.pid
        
        # Try reverse substring (process name contains search term)
        search_lower = process_name.lower()
        for proc in processes:
            if proc.name.lower() in search_lower or 'cyberpunk' in proc.name.lower() or '2077' in proc.name.lower():
                logger.info(f"Found process (fuzzy): {proc.name} (PID: {proc.pid})")
                return proc.pid
        
        logger.warning(f"Process '{process_name}' not found")
        logger.debug(f"Available processes: {[p.name for p in processes[:10]]}")
        return None
    except Exception as e:
        logger.error(f"Failed to enumerate processes: {e}", exc_info=True)
        return None


def load_static_functions(static_functions_json: Path, max_functions: int, logger: logging.Logger) -> List[int]:
    """Load function offsets from static analysis JSON."""
    function_offsets = []
    
    if not static_functions_json or not static_functions_json.exists():
        logger.warning("Static functions file not provided or not found")
        return function_offsets
    
    try:
        logger.info(f"Loading static functions from {static_functions_json}")
        with open(static_functions_json, 'r') as f:
            static_data = json.load(f)
        
        # Get function offsets from static analysis
        functions = static_data.get("functions", {})
        logger.debug(f"Found {len(functions)} functions in static analysis")
        
        for addr_str, func_data in functions.items():
            try:
                offset = int(func_data.get("offset", "0x0"), 16)
                if offset > 0 and offset < 0x10000000:  # Valid range
                    function_offsets.append(offset)
            except (ValueError, KeyError) as e:
                logger.debug(f"Failed to parse function offset: {e}")
                continue
        
        # Limit to avoid hooking too many functions
        original_count = len(function_offsets)
        function_offsets = function_offsets[:max_functions]
        
        if original_count > max_functions:
            logger.warning(f"Limited functions from {original_count} to {max_functions}")
        
        logger.info(f"Loaded {len(function_offsets)} function addresses from static analysis")
        return function_offsets
        
    except json.JSONDecodeError as e:
        logger.error(f"Invalid JSON in static functions file: {e}")
        return []
    except Exception as e:
        logger.error(f"Failed to load static functions: {e}", exc_info=True)
        return []


def discover_functions_dynamic(
    process_name: str,
    trace_duration: int,
    min_call_count: int,
    static_functions_json: Optional[Path],
    max_functions_to_hook: int,
    burst_mode: bool,
    burst_duration: int,
    burst_interval: int,
    sample_rate: int,
    logger: logging.Logger,
    metrics: ProgressMetrics
) -> Tuple[Dict[int, FunctionInfo], Dict[int, List[int]]]:
    """
    Discover functions via dynamic analysis using frida-python.
    
    Enhanced with error handling, logging, and progress tracking.
    """
    if not FRIDA_AVAILABLE:
        logger.error("frida-python not available")
        return {}, {}
    
    logger.info(f"Starting dynamic discovery (duration: {trace_duration}s, sample_rate: {sample_rate})")
    
    # Load static functions to hook
    function_offsets_to_hook = load_static_functions(static_functions_json, max_functions_to_hook, logger)
    
    # Find process
    pid = find_process(process_name, logger)
    if not pid:
        logger.error(f"Process '{process_name}' not found. Please start the game first.")
        return {}, {}
    
    try:
        # Attach to process
        logger.info(f"Attaching to process {pid}")
        device = frida.get_local_device()
        session = device.attach(pid)
        logger.info(f"Successfully attached to process {pid}")
        
        # Inject JavaScript code with configuration
        frida_script = FRIDA_DISCOVERY_SCRIPT_TEMPLATE.format(
            SAMPLE_RATE=sample_rate,
            LOG_LEVEL=logger.level
        )
        
        script = session.create_script(frida_script)
        
        # Handle script messages
        def on_message(message, data):
            if message['type'] == 'send':
                logger.debug(f"[FRIDA] {message.get('payload', '')}")
            elif message['type'] == 'error':
                logger.error(f"[FRIDA ERROR] {message.get('stack', message.get('description', 'Unknown error'))}")
                metrics.errors += 1
        
        script.on('message', on_message)
        
        try:
            script.load()
            logger.info("JavaScript script injected successfully")
        except Exception as e:
            logger.error(f"Failed to load script: {e}", exc_info=True)
            session.detach()
            return {}, {}
        
        # Hook specific functions from static analysis
        if function_offsets_to_hook:
            logger.info(f"Hooking {len(function_offsets_to_hook)} specific functions...")
            hooked_count = 0
            failed_count = 0
            
            for i, offset in enumerate(function_offsets_to_hook):
                try:
                    success = script.exports.hook_function(offset)
                    if success:
                        hooked_count += 1
                    else:
                        failed_count += 1
                    
                    if (i + 1) % 100 == 0:
                        logger.info(f"  Hooked {hooked_count}/{i + 1} functions ({failed_count} failed)...")
                        metrics.update(functions_hooked=hooked_count)
                except Exception as e:
                    logger.debug(f"Failed to hook function at offset 0x{offset:X}: {e}")
                    failed_count += 1
                    continue
            
            metrics.update(functions_hooked=hooked_count)
            logger.info(f"Successfully hooked {hooked_count} functions ({failed_count} failed)")
            
            # Get hook statistics
            try:
                hook_stats = script.exports.getHookStats()
                logger.info(f"Hook statistics: {hook_stats}")
            except:
                pass
        else:
            logger.warning("No static functions provided - minimal discovery")
        
        # Wait for tracing duration (burst mode or continuous)
        if burst_mode:
            logger.info(f"BURST MODE: Hooking for {burst_duration}s, then unhooking for {burst_interval}s")
            logger.info(f"Total duration: {trace_duration}s")
            
            start_time = time.time()
            burst_num = 0
            all_functions = {}
            all_call_graph = {}
            
            while time.time() - start_time < trace_duration:
                burst_num += 1
                elapsed_total = int(time.time() - start_time)
                
                # Hook phase
                logger.info(f"BURST #{burst_num}: Hooking for {burst_duration}s...")
                hook_start = time.time()
                
                # Re-hook functions
                if function_offsets_to_hook:
                    rehooked = 0
                    for offset in function_offsets_to_hook[:max_functions_to_hook]:
                        try:
                            script.exports.hook_function(offset)
                            rehooked += 1
                        except:
                            pass
                    logger.debug(f"Re-hooked {rehooked} functions")
                
                # Collect data during hook phase
                while time.time() - hook_start < burst_duration:
                    time.sleep(1)
                    elapsed_burst = int(time.time() - hook_start)
                    
                    if elapsed_burst % 2 == 0:
                        try:
                            stats = script.exports.get_stats()
                            logger.info(f"  [{elapsed_burst}s] Functions: {stats['functionCount']}, Calls: {stats['totalCalls']}")
                            metrics.update(
                                functions_called=stats['functionCount'],
                                total_calls=stats['totalCalls'],
                                call_graph_edges=stats['callGraphSize']
                            )
                        except Exception as e:
                            logger.debug(f"Failed to get stats: {e}")
                    
                    # Check total time limit
                    if time.time() - start_time >= trace_duration:
                        break
                
                # Collect results from this burst
                try:
                    funcs_data = script.exports.get_functions()
                    call_graph_data = script.exports.get_call_graph()
                    
                    # Merge into accumulated results
                    for func_data in funcs_data:
                        offset = func_data['offset']
                        address = IMAGE_BASE + offset
                        if address not in all_functions:
                            all_functions[address] = {
                                'offset': offset,
                                'address': address,
                                'callCount': 0,
                                'callers': [],
                                'callees': []
                            }
                        all_functions[address]['callCount'] += func_data.get('callCount', 0)
                        all_functions[address]['callers'].extend(func_data.get('callers', []))
                    
                    for caller_str, callees in call_graph_data.items():
                        caller_addr = IMAGE_BASE + int(caller_str)
                        if caller_addr not in all_call_graph:
                            all_call_graph[caller_addr] = []
                        all_call_graph[caller_addr].extend([IMAGE_BASE + c for c in callees])
                    
                    stats = script.exports.get_stats()
                    logger.info(f"Burst #{burst_num} complete: {stats['functionCount']} functions, {stats['totalCalls']} calls")
                except Exception as e:
                    logger.warning(f"Failed to collect burst results: {e}")
                
                # Unhook phase
                if time.time() - start_time < trace_duration:
                    logger.info(f"Unhooking for {burst_interval}s (letting game run normally)...")
                    try:
                        unhooked_count = script.exports.unhookAll()
                        logger.debug(f"Stopped tracking {unhooked_count} functions")
                    except Exception as e:
                        logger.debug(f"Failed to unhook: {e}")
                    
                    time.sleep(burst_interval)
            
            # Convert accumulated results
            functions = {}
            for addr_str, func_data in all_functions.items():
                if func_data['callCount'] < min_call_count:
                    continue
                functions[func_data['address']] = FunctionInfo(
                    offset=func_data['offset'],
                    address=func_data['address'],
                    confidence="high",
                    discovery_method="dynamic",
                    call_count=func_data['callCount'],
                    callers=list(set([IMAGE_BASE + c for c in func_data['callers']])),
                    callees=[]
                )
            
            # Build call graph
            call_graph = {}
            for caller_addr, callees in all_call_graph.items():
                call_graph[caller_addr] = list(set(callees))
                if caller_addr in functions:
                    functions[caller_addr].callees = call_graph[caller_addr]
            
            metrics.update(elapsed_time=time.time() - start_time)
            logger.info(f"Burst mode complete: {len(functions)} functions, {len(call_graph)} call relationships")
            return functions, call_graph
            
        else:
            # Normal continuous hooking mode
            logger.info(f"Tracing for {trace_duration} seconds...")
            logger.info("(Interact with the game to trigger function calls)")
            
            start_time = time.time()
            last_stats_time = start_time
            
            while time.time() - start_time < trace_duration:
                time.sleep(1)
                elapsed = int(time.time() - start_time)
                
                # Update metrics every second
                metrics.update(elapsed_time=elapsed)
                
                # Log stats every 10 seconds
                if time.time() - last_stats_time >= 10:
                    try:
                        stats = script.exports.get_stats()
                        logger.info(f"[{elapsed}s] Functions: {stats['functionCount']}, Calls: {stats['totalCalls']}, Edges: {stats['callGraphSize']}")
                        metrics.update(
                            functions_called=stats['functionCount'],
                            total_calls=stats['totalCalls'],
                            call_graph_edges=stats['callGraphSize']
                        )
                        last_stats_time = time.time()
                    except Exception as e:
                        logger.debug(f"Failed to get stats: {e}")
            
            # Collect results
            try:
                funcs_data = script.exports.get_functions()
                call_graph_data = script.exports.get_call_graph()
                stats = script.exports.get_stats()
                
                logger.info(f"Dynamic discovery stats: {stats}")
                
                # Convert to FunctionInfo objects
                functions = {}
                for func_data in funcs_data:
                    if func_data['callCount'] < min_call_count:
                        continue
                        
                    offset = func_data['offset']
                    address = IMAGE_BASE + offset
                    functions[address] = FunctionInfo(
                        offset=offset,
                        address=address,
                        confidence="high",
                        discovery_method="dynamic",
                        call_count=func_data.get('callCount', 0),
                        callers=[IMAGE_BASE + c for c in func_data.get('callers', [])],
                        callees=[]
                    )
                
                # Build call graph
                call_graph = {}
                for caller_str, callees in call_graph_data.items():
                    try:
                        caller_addr = IMAGE_BASE + int(caller_str)
                        call_graph[caller_addr] = [IMAGE_BASE + c for c in callees]
                        
                        # Update callees in function info
                        if caller_addr in functions:
                            functions[caller_addr].callees = call_graph[caller_addr]
                    except (ValueError, TypeError) as e:
                        logger.debug(f"Failed to parse call graph entry: {e}")
                        continue
                
                metrics.update(elapsed_time=time.time() - start_time)
                logger.info(f"Dynamic discovery: Found {len(functions)} functions, {len(call_graph)} call relationships")
                
                return functions, call_graph
                
            except Exception as e:
                logger.error(f"Failed to collect results: {e}", exc_info=True)
                return {}, {}
        
    except frida.ProcessNotFoundError:
        logger.error(f"Process '{process_name}' not found")
        return {}, {}
    except frida.InvalidArgumentError as e:
        logger.error(f"Invalid argument: {e}")
        return {}, {}
    except Exception as e:
        logger.error(f"Frida error: {e}", exc_info=True)
        return {}, {}
    
    finally:
        try:
            script.unload()
            session.detach()
            logger.info("Detached from process")
        except Exception as e:
            logger.debug(f"Error during cleanup: {e}")


def export_dynamic_results(
    output_path: Path,
    functions: Dict[int, FunctionInfo],
    call_graph: Dict[int, List[int]],
    signatures: Dict[int, Dict],
    process_name: str,
    process_id: int,
    trace_duration: int,
    metrics: ProgressMetrics,
    logger: logging.Logger
) -> None:
    """
    Export dynamic analysis results to JSON with improved structure.
    
    Enhanced format includes:
    - Metadata (version, timestamps, configuration)
    - Metrics (performance, statistics)
    - Functions (with full details)
    - Call graph (structured edges)
    - Signatures (if available)
    """
    logger.info(f"Exporting results to {output_path}...")
    
    # Convert functions to dict format
    functions_dict = {}
    for addr, func in functions.items():
        functions_dict[f"0x{addr:X}"] = func.to_dict()
    
    # Build call graph edges
    edges = []
    for caller, callees in call_graph.items():
        for callee in callees:
            edges.append(CallGraphEdge(caller=caller, callee=callee))
    
    # Build improved output structure
    output = {
        "metadata": {
            "version": "2.0",
            "format_version": "2.0",
            "analysis_type": "dynamic",
            "timestamp": datetime.now().isoformat(),
            "analysis_date": datetime.now().strftime("%Y-%m-%d"),
            "analysis_time": datetime.now().strftime("%H:%M:%S"),
            "tool": "dynamic_analysis.py",
            "frida_version": frida.__version__ if FRIDA_AVAILABLE else "unknown"
        },
        "configuration": {
            "process_name": process_name,
            "process_id": process_id,
            "trace_duration_seconds": trace_duration,
            "sample_rate": 10,  # TODO: Pass from args
            "min_call_count": 1,  # TODO: Pass from args
        },
        "metrics": metrics.to_dict(),
        "functions": functions_dict,
        "call_graph": {
            "edges": [e.to_dict() for e in edges],
            "edge_count": len(edges),
            "node_count": len(set([e.caller for e in edges] + [e.callee for e in edges]))
        },
        "signatures": {f"0x{k:X}": v for k, v in signatures.items()} if signatures else {},
        "statistics": {
            "total_functions": len(functions),
            "functions_with_calls": len([f for f in functions.values() if f.call_count > 0]),
            "call_edges": len(edges),
            "total_calls": sum(f.call_count for f in functions.values()),
            "average_calls_per_function": round(
                sum(f.call_count for f in functions.values()) / len(functions) if functions else 0,
                2
            ),
            "max_calls": max((f.call_count for f in functions.values()), default=0),
            "functions_with_callers": len([f for f in functions.values() if f.callers]),
            "functions_with_callees": len([f for f in functions.values() if f.callees])
        }
    }
    
    # Write JSON with pretty formatting
    try:
        with open(output_path, 'w') as f:
            json.dump(output, f, indent=2, sort_keys=False)
        logger.info(f"Successfully exported {len(functions)} functions and {len(edges)} call edges")
        logger.info(f"Output file size: {output_path.stat().st_size / 1024 / 1024:.2f} MB")
    except Exception as e:
        logger.error(f"Failed to write output file: {e}", exc_info=True)
        raise


def main():
    parser = argparse.ArgumentParser(
        description="Dynamic Analysis - Discover functions via frida-python (Enhanced)",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  # Basic usage
  python3 dynamic_analysis.py --process-name "Cyberpunk" --static-functions static_functions.json
  
  # Safe burst mode
  python3 dynamic_analysis.py --process-name "Cyberpunk" --burst-mode --sample-rate 20 --max-functions 50
  
  # With logging
  python3 dynamic_analysis.py --process-name "Cyberpunk" --log-level DEBUG --log-file analysis.log
        """
    )
    
    parser.add_argument('--process-name', type=str, default='Cyberpunk2077',
                       help='Process name to attach to (default: Cyberpunk2077)')
    parser.add_argument('--output', type=Path, default=Path('dynamic_functions.json'),
                       help='Output JSON file (default: dynamic_functions.json)')
    parser.add_argument('--trace-duration', type=int, default=30,
                       help='Duration to trace execution in seconds (default: 30)')
    parser.add_argument('--min-call-count', type=int, default=1,
                       help='Minimum call count to include function (default: 1)')
    parser.add_argument('--static-functions', type=Path,
                       help='Path to static_functions.json to hook specific functions (safer)')
    parser.add_argument('--max-functions', type=int, default=1000,
                       help='Maximum number of functions to hook (default: 1000, prevents crashes)')
    parser.add_argument('--burst-mode', action='store_true',
                       help='Use burst hooking: hook for short periods, then unhook (safer, prevents crashes)')
    parser.add_argument('--burst-duration', type=int, default=5,
                       help='Duration to hook in burst mode (seconds, default: 5)')
    parser.add_argument('--burst-interval', type=int, default=10,
                       help='Duration to unhook between bursts (seconds, default: 10)')
    parser.add_argument('--sample-rate', type=int, default=10,
                       help='Sample rate for hook tracking (track every Nth call, default: 10, reduces overhead)')
    parser.add_argument('--log-level', type=str, default='INFO',
                       choices=['DEBUG', 'INFO', 'WARNING', 'ERROR', 'CRITICAL'],
                       help='Log level (default: INFO)')
    parser.add_argument('--log-file', type=Path,
                       help='Log file path (optional, logs to file in addition to console)')
    
    args = parser.parse_args()
    
    if not FRIDA_AVAILABLE:
        print("[ERROR] frida-python is required for dynamic analysis")
        print("[ERROR] Install with: pip install frida")
        sys.exit(1)
    
    # Setup logging
    logger = setup_logging(args.log_level, args.log_file)
    
    # Initialize metrics
    metrics = ProgressMetrics(start_time=time.time())
    
    logger.info("=" * 70)
    logger.info("Dynamic Analysis - Function Discovery (Enhanced)")
    logger.info("=" * 70)
    logger.info(f"Process: {args.process_name}")
    logger.info(f"Trace Duration: {args.trace_duration}s")
    logger.info(f"Output: {args.output}")
    logger.info(f"Sample Rate: {args.sample_rate}")
    logger.info(f"Burst Mode: {args.burst_mode}")
    if args.burst_mode:
        logger.info(f"  Burst Duration: {args.burst_duration}s")
        logger.info(f"  Burst Interval: {args.burst_interval}s")
    logger.info("")
    
    # Discover functions
    functions, call_graph = discover_functions_dynamic(
        args.process_name,
        args.trace_duration,
        args.min_call_count,
        args.static_functions,
        args.max_functions,
        args.burst_mode,
        args.burst_duration,
        args.burst_interval,
        args.sample_rate,
        logger,
        metrics
    )
    
    if not functions:
        logger.error("No functions discovered. Is the game running?")
        sys.exit(1)
    
    # Infer signatures (placeholder)
    signatures = {}
    
    # Find process ID for export
    process_id = find_process(args.process_name, logger) or 0
    
    # Export results
    export_dynamic_results(
        args.output,
        functions,
        call_graph,
        signatures,
        args.process_name,
        process_id,
        args.trace_duration,
        metrics,
        logger
    )
    
    # Final summary
    logger.info("=" * 70)
    logger.info("Analysis Complete")
    logger.info("=" * 70)
    logger.info(f"Functions discovered: {len(functions)}")
    logger.info(f"Call graph edges: {len(call_graph)}")
    logger.info(f"Total calls tracked: {metrics.total_calls}")
    logger.info(f"Analysis duration: {metrics.elapsed_time:.2f}s")
    logger.info(f"Output file: {args.output}")
    if metrics.errors > 0:
        logger.warning(f"Errors encountered: {metrics.errors}")
    if metrics.warnings > 0:
        logger.warning(f"Warnings: {metrics.warnings}")


if __name__ == '__main__':
    main()
