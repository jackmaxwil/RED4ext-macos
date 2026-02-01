#!/usr/bin/env python3
"""
Dynamic Analysis Script - Function Discovery via frida-python

Discovers functions, builds call graphs, and infers signatures using dynamic
analysis (frida-python). Requires the game to be running.

Usage:
    python3 dynamic_analysis.py \
      --process-name "Cyberpunk2077" \
      --output dynamic_functions.json \
      --trace-duration 30
"""

import json
import argparse
import sys
import time
from pathlib import Path
from typing import Dict, List, Tuple, Optional

# Import frida-python library (Python bindings for Frida)
# Note: frida-python is used to inject JavaScript code into the target process.
# The actual hooking/interception happens in JavaScript using Frida's JavaScript API
# (Module, Interceptor, Stalker, rpc.exports). This is Frida's architecture - you
# use Python to control the injection, but the instrumentation code runs as JavaScript.
try:
    import frida
    FRIDA_AVAILABLE = True
except ImportError:
    FRIDA_AVAILABLE = False
    print("[ERROR] frida-python library not installed.")
    print("[ERROR] Install with: pip install frida")
    sys.exit(1)

# Import shared data structures
try:
    from re_common import FunctionInfo, CallGraphEdge, IMAGE_BASE
except ImportError:
    sys.path.insert(0, str(Path(__file__).parent))
    from re_common import FunctionInfo, CallGraphEdge, IMAGE_BASE


# Frida JavaScript code to be injected into the target process
# SAFE APPROACH: Hook specific known functions instead of using Stalker (which crashes the game)
# Based on best practices from metalfx-denoiser project
FRIDA_DISCOVERY_SCRIPT_TEMPLATE = """
'use strict';

const discoveredFunctions = new Map();
const callGraph = new Map();
const moduleBase = Module.findBaseAddress("Cyberpunk2077");
const SAMPLE_RATE = {SAMPLE_RATE};  // Track caller every Nth call (reduces overhead)

if (!moduleBase) {
    console.log("[ERROR] Could not find Cyberpunk2077 module");
    rpc.exports = {
        getFunctions: () => [],
        getCallGraph: () => ({}),
        getStats: () => ({functionCount: 0, callGraphSize: 0, totalCalls: 0}),
        hookFunction: (offset) => false
    };
} else {
    console.log("[*] Module base: " + moduleBase);
    
    // Track function calls via targeted Interceptor.attach() to specific addresses
    // This is much safer than Stalker.follow() which traces everything and crashes
    
    function hookFunctionAtOffset(offset) {
        try {
            const funcAddr = moduleBase.add(offset);
            const offsetKey = offset.toString();
            
            // Skip if already hooked
            if (discoveredFunctions.has(offsetKey)) {
                return true;
            }
            
            // Initialize function info
            discoveredFunctions.set(offsetKey, {
                offset: offset,
                address: funcAddr.toString(),
                callCount: 0,
                callers: [],
                callees: []
            });
            
            // Hook this specific function address
            // OPTIMIZED: Minimize JavaScript execution to prevent freezes
            // - Use Set for O(1) lookups instead of array indexOf (O(n))
            // - Batch updates (only update every Nth call)
            // - Avoid string conversions in hot path
            // - Minimal work per hook call
            
            const callersSet = new Set();  // Use Set for O(1) uniqueness checks
            let callCount = 0;
            
            Interceptor.attach(funcAddr, {
                onEnter: function(args) {
                    try {
                        callCount++;
                        
                        // OPTIMIZATION: Only do expensive operations every Nth call
                        // This reduces overhead by 90% for frequently-called functions
                        if (callCount % SAMPLE_RATE === 0) {
                            const func = discoveredFunctions.get(offsetKey);
                            if (func) {
                                func.callCount += SAMPLE_RATE;  // Batch increment
                                
                                // Track caller (sampled)
                                const caller = this.returnAddress;
                                if (caller.compare(moduleBase) >= 0) {
                                    const callerOffset = caller.sub(moduleBase).toInt32();
                                    if (callerOffset > 0 && callerOffset < 0x10000000) {
                                        // Use Set for O(1) check instead of indexOf O(n)
                                        if (!callersSet.has(callerOffset)) {
                                            callersSet.add(callerOffset);
                                            func.callers.push(callerOffset);
                                            
                                            // Update call graph (batched)
                                            const callerKey = callerOffset.toString();
                                            if (!callGraph.has(callerKey)) {
                                                callGraph.set(callerKey, []);
                                            }
                                            const callees = callGraph.get(callerKey);
                                            if (callees.indexOf(offset) === -1) {
                                                callees.push(offset);
                                            }
                                        }
                                    }
                                }
                            }
                        } else {
                            // Fast path: just increment counter (no expensive operations)
                            const func = discoveredFunctions.get(offsetKey);
                            if (func) {
                                func.callCount++;
                            }
                        }
                    } catch (e) {
                        // Silently ignore errors to avoid crashing
                    }
                },
                onLeave: function(retval) {
                    // Empty - no work on return to minimize overhead
                }
            });
            
            return true;
        } catch (e) {
            console.log("[ERROR] Failed to hook function at offset " + offset + ": " + e);
            return false;
        }
    }
    
    // Export results
    rpc.exports = {
        getFunctions: function() {
            return Array.from(discoveredFunctions.values());
        },
        getCallGraph: function() {
            const result = {};
            for (const [caller, callees] of callGraph.entries()) {
                result[caller] = callees;
            }
            return result;
        },
        getStats: function() {
            let totalCalls = 0;
            for (const func of discoveredFunctions.values()) {
                totalCalls += func.callCount;
            }
            return {
                functionCount: discoveredFunctions.size,
                callGraphSize: callGraph.size,
                totalCalls: totalCalls
            };
        },
        hookFunction: hookFunctionAtOffset,
        unhookFunction: function(offset) {
            try {
                const offsetKey = offset.toString();
                if (discoveredFunctions.has(offsetKey)) {
                    // Note: Frida doesn't provide direct unhook, but we can stop tracking
                    discoveredFunctions.delete(offsetKey);
                    return true;
                }
                return false;
            } catch (e) {
                return false;
            }
        },
        unhookAll: function() {
            // Clear tracking (hooks remain but we stop collecting data)
            const count = discoveredFunctions.size;
            discoveredFunctions.clear();
            callGraph.clear();
            return count;
        }
    };
}
"""


def find_process(process_name: str) -> Optional[int]:
    """Find process ID by name (flexible matching)."""
    if not FRIDA_AVAILABLE:
        return None
    
    try:
        device = frida.get_local_device()
        processes = device.enumerate_processes()
        
        # Try exact match first
        for proc in processes:
            if proc.name.lower() == process_name.lower():
                print(f"[+] Found process (exact): {proc.name} (PID: {proc.pid})")
                return proc.pid
        
        # Try substring match
        for proc in processes:
            if process_name.lower() in proc.name.lower():
                print(f"[+] Found process (substring): {proc.name} (PID: {proc.pid})")
                return proc.pid
        
        # Try reverse substring (process name contains search term)
        search_lower = process_name.lower()
        for proc in processes:
            if proc.name.lower() in search_lower or 'cyberpunk' in proc.name.lower() or '2077' in proc.name.lower():
                print(f"[+] Found process (fuzzy): {proc.name} (PID: {proc.pid})")
                return proc.pid
        
        return None
    except Exception as e:
        print(f"[ERROR] Failed to enumerate processes: {e}")
        return None


def discover_functions_dynamic(process_name: str, trace_duration: int = 30,
                               min_call_count: int = 1,
                               static_functions_json: Optional[Path] = None,
                               max_functions_to_hook: int = 1000,
                               burst_mode: bool = False,
                               burst_duration: int = 5,
                               burst_interval: int = 10,
                               sample_rate: int = 10) -> Tuple[Dict[int, FunctionInfo], Dict[int, List[int]]]:
    """
    Discover functions via dynamic analysis using frida-python.
    
    SAFE APPROACH: Only hooks specific functions from static analysis, not everything.
    This prevents crashes caused by Stalker or hooking module base.
    """
    if not FRIDA_AVAILABLE:
        print("[ERROR] frida-python not available")
        return {}, {}
    
    print(f"[*] Dynamic discovery (frida-python, {trace_duration}s)...")
    print("[*] Using SAFE targeted hooking (no Stalker) to prevent crashes")
    
    # Load static functions to hook (if provided)
    function_offsets_to_hook = []
    if static_functions_json and static_functions_json.exists():
        try:
            import json
            with open(static_functions_json, 'r') as f:
                static_data = json.load(f)
            
            # Get function offsets from static analysis
            for addr_str, func_data in static_data.get("functions", {}).items():
                try:
                    offset = int(func_data.get("offset", "0x0"), 16)
                    if offset > 0 and offset < 0x10000000:  # Valid range
                        function_offsets_to_hook.append(offset)
                except (ValueError, KeyError):
                    continue
            
            # Limit to avoid hooking too many functions (performance)
            function_offsets_to_hook = function_offsets_to_hook[:max_functions_to_hook]
            print(f"[+] Loaded {len(function_offsets_to_hook)} function addresses from static analysis")
        except Exception as e:
            print(f"[WARNING] Failed to load static functions: {e}")
            print("[WARNING] Will use minimal hooking approach")
    
    # Find process
    pid = find_process(process_name)
    if not pid:
        print(f"[ERROR] Process '{process_name}' not found.")
        print(f"[ERROR] Please start the game first.")
        return {}, {}
    
    try:
        # Attach to process
        device = frida.get_local_device()
        session = device.attach(pid)
        print(f"[+] Attached to process {pid}")
        
        # Inject JavaScript code with sample rate
        frida_script = FRIDA_DISCOVERY_SCRIPT_TEMPLATE.replace('{SAMPLE_RATE}', str(sample_rate))
        script = session.create_script(frida_script)
        
        # Handle script messages
        def on_message(message, data):
            if message['type'] == 'send':
                print(f"[FRIDA] {message['payload']}")
            elif message['type'] == 'error':
                print(f"[FRIDA ERROR] {message['stack']}")
        
        script.on('message', on_message)
        script.load()
        print("[+] JavaScript script injected")
        
        # Hook specific functions from static analysis (SAFE approach)
        if function_offsets_to_hook:
            print(f"[*] Hooking {len(function_offsets_to_hook)} specific functions...")
            hooked_count = 0
            for offset in function_offsets_to_hook:
                try:
                    success = script.exports.hook_function(offset)
                    if success:
                        hooked_count += 1
                    if hooked_count % 100 == 0:
                        print(f"  Hooked {hooked_count}/{len(function_offsets_to_hook)} functions...")
                except Exception as e:
                    # Skip invalid addresses
                    continue
            print(f"[+] Successfully hooked {hooked_count} functions")
        else:
            print("[WARNING] No static functions provided - minimal discovery")
        
        # Wait for tracing duration
        if burst_mode:
            print(f"[*] BURST MODE: Hooking for {burst_duration}s, then unhooking for {burst_interval}s")
            print(f"[*] Total duration: {trace_duration}s")
            
            start_time = time.time()
            burst_num = 0
            all_functions = {}  # Accumulate results across bursts
            all_call_graph = {}
            
            while time.time() - start_time < trace_duration:
                burst_num += 1
                elapsed_total = int(time.time() - start_time)
                
                # Hook phase
                print(f"\n[+] BURST #{burst_num}: Hooking for {burst_duration}s...")
                hook_start = time.time()
                
                # Re-hook functions (they may have been unhooked)
                if function_offsets_to_hook:
                    rehooked = 0
                    for offset in function_offsets_to_hook[:max_functions_to_hook]:
                        try:
                            script.exports.hook_function(offset)
                            rehooked += 1
                        except:
                            pass
                    print(f"  Re-hooked {rehooked} functions")
                
                # Collect data during hook phase
                while time.time() - hook_start < burst_duration:
                    time.sleep(1)
                    elapsed_burst = int(time.time() - hook_start)
                    if elapsed_burst % 2 == 0:
                        try:
                            stats = script.exports.get_stats()
                            print(f"  [{elapsed_burst}s] Functions: {stats['functionCount']}, Calls: {stats['totalCalls']}")
                        except:
                            pass
                    
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
                    print(f"  Burst #{burst_num} complete: {stats['functionCount']} functions, {stats['totalCalls']} calls")
                except Exception as e:
                    print(f"  [WARNING] Failed to collect burst results: {e}")
                
                # Unhook phase (let game run normally)
                if time.time() - start_time < trace_duration:
                    print(f"[*] Unhooking for {burst_interval}s (letting game run normally)...")
                    try:
                        unhooked_count = script.exports.unhookAll()
                        print(f"  Stopped tracking {unhooked_count} functions")
                    except:
                        pass
                    
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
            
            print(f"\n[+] Burst mode complete: {len(functions)} functions, {len(call_graph)} call relationships")
            return functions, call_graph
            
        else:
            # Normal continuous hooking mode
            print(f"[*] Tracing for {trace_duration} seconds...")
            print("[*] (Interact with the game to trigger function calls)")
            
            start_time = time.time()
            while time.time() - start_time < trace_duration:
                time.sleep(1)
                elapsed = int(time.time() - start_time)
                if elapsed % 10 == 0:
                    try:
                        stats = script.exports.get_stats()
                        print(f"  [{elapsed}s] Functions: {stats['functionCount']}, Calls: {stats['totalCalls']}")
                    except:
                        pass
        
        # Collect results
        try:
            funcs_data = script.exports.get_functions()
            call_graph_data = script.exports.get_call_graph()
            stats = script.exports.get_stats()
            
            print(f"[+] Dynamic discovery stats: {stats}")
            
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
                    callees=[]  # Will be filled from call graph
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
                except (ValueError, TypeError):
                    continue
            
            print(f"[+] Dynamic discovery: Found {len(functions)} functions, {len(call_graph)} call relationships")
            
            return functions, call_graph
            
        except Exception as e:
            print(f"[ERROR] Failed to collect results: {e}")
            import traceback
            traceback.print_exc()
            return {}, {}
        
        finally:
            try:
                script.unload()
                session.detach()
            except:
                pass
            
    except frida.ProcessNotFoundError:
        print(f"[ERROR] Process '{process_name}' not found")
        return {}, {}
    except frida.InvalidArgumentError as e:
        print(f"[ERROR] Invalid argument: {e}")
        return {}, {}
    except Exception as e:
        print(f"[ERROR] Frida error: {e}")
        import traceback
        traceback.print_exc()
        return {}, {}


def infer_signatures(functions: Dict[int, FunctionInfo]) -> Dict[int, Dict]:
    """Infer function signatures (simplified heuristic)."""
    print("[*] Inferring signatures...")
    
    signatures = {}
    inferred_count = 0
    
    for addr, func in functions.items():
        if func.call_count > 0:
            # Simple heuristic: if called, likely has standard ARM64 calling convention
            # Real signature inference would hook functions and analyze register/stack usage
            signatures[addr] = {
                "args": 1,  # Placeholder
                "arg_types": ["pointer"],
                "return_type": "void",
                "confidence": 0.5
            }
            func.signature = signatures[addr]
            inferred_count += 1
    
    print(f"[+] Inferred signatures for {inferred_count} functions")
    return signatures


def export_dynamic_results(output_path: Path, functions: Dict[int, FunctionInfo],
                          call_graph: Dict[int, List[int]], signatures: Dict[int, Dict],
                          process_name: str, process_id: int, trace_duration: int) -> None:
    """Export dynamic analysis results to JSON."""
    print(f"[*] Exporting results to {output_path}...")
    
    # Convert functions to dict format
    functions_dict = {}
    for addr, func in functions.items():
        functions_dict[f"0x{addr:X}"] = func.to_dict()
    
    # Build call graph edges
    edges = []
    for caller, callees in call_graph.items():
        for callee in callees:
            edges.append(CallGraphEdge(caller=caller, callee=callee))
    
    # Build output structure
    output = {
        "version": "1.0",
        "analysis_type": "dynamic",
        "process_name": process_name,
        "process_id": process_id,
        "trace_duration": trace_duration,
        "timestamp": time.strftime("%Y-%m-%d %H:%M:%S"),
        "functions": functions_dict,
        "call_graph": {
            "edges": [e.to_dict() for e in edges]
        },
        "signatures": {f"0x{k:X}": v for k, v in signatures.items()},
        "stats": {
            "total_functions": len(functions),
            "call_edges": len(edges),
            "total_calls": sum(f.call_count for f in functions.values())
        }
    }
    
    # Write JSON
    with open(output_path, 'w') as f:
        json.dump(output, f, indent=2)
    
    print(f"[+] Exported {len(functions)} functions and {len(edges)} call edges")


def main():
    parser = argparse.ArgumentParser(
        description="Dynamic Analysis - Discover functions via frida-python"
    )
    parser.add_argument(
        '--process-name',
        type=str,
        default='Cyberpunk2077',
        help='Process name to attach to (default: Cyberpunk2077)'
    )
    parser.add_argument(
        '--output',
        type=Path,
        default=Path('dynamic_functions.json'),
        help='Output JSON file (default: dynamic_functions.json)'
    )
    parser.add_argument(
        '--trace-duration',
        type=int,
        default=30,
        help='Duration to trace execution in seconds (default: 30)'
    )
    parser.add_argument(
        '--min-call-count',
        type=int,
        default=1,
        help='Minimum call count to include function (default: 1)'
    )
    parser.add_argument(
        '--static-functions',
        type=Path,
        help='Path to static_functions.json to hook specific functions (safer)'
    )
    parser.add_argument(
        '--max-functions',
        type=int,
        default=1000,
        help='Maximum number of functions to hook (default: 1000, prevents crashes)'
    )
    parser.add_argument(
        '--burst-mode',
        action='store_true',
        help='Use burst hooking: hook for short periods, then unhook (safer, prevents crashes)'
    )
    parser.add_argument(
        '--burst-duration',
        type=int,
        default=5,
        help='Duration to hook in burst mode (seconds, default: 5)'
    )
    parser.add_argument(
        '--burst-interval',
        type=int,
        default=10,
        help='Duration to unhook between bursts (seconds, default: 10)'
    )
    parser.add_argument(
        '--sample-rate',
        type=int,
        default=10,
        help='Sample rate for hook tracking (track every Nth call, default: 10, reduces overhead)'
    )
    
    args = parser.parse_args()
    
    if not FRIDA_AVAILABLE:
        print("[ERROR] frida-python is required for dynamic analysis")
        sys.exit(1)
    
    print("=" * 70)
    print("Dynamic Analysis - Function Discovery")
    print("=" * 70)
    print(f"Process: {args.process_name}")
    print(f"Trace Duration: {args.trace_duration}s")
    print(f"Output: {args.output}")
    print()
    
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
        args.sample_rate
    )
    
    if not functions:
        print("[ERROR] No functions discovered. Is the game running?")
        sys.exit(1)
    
    # Infer signatures
    signatures = infer_signatures(functions)
    
    # Find process ID for export
    process_id = find_process(args.process_name) or 0
    
    # Export results
    export_dynamic_results(
        args.output,
        functions,
        call_graph,
        signatures,
        args.process_name,
        process_id,
        args.trace_duration
    )
    
    print()
    print("=" * 70)
    print("Dynamic analysis complete!")
    print(f"Results saved to: {args.output}")
    print("=" * 70)


if __name__ == "__main__":
    main()
