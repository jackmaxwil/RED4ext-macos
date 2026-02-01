# Frida-Based Reverse Engineering Analysis for Cyberpunk 2077

> Feasibility study: Using Frida (Gum + frida-tools) to automatically reverse engineer Cyberpunk 2077 into a high-fidelity, navigable code representation on macOS ARM64.

**Analysis Date:** January 2026  
**Target:** `Cyberpunk2077.app/Contents/MacOS/Cyberpunk2077` (Mach-O ARM64)  
**Tool:** Frida Gum + frida-tools

---

## Executive Summary

### Recommendation: **PARTIAL** - Hybrid Approach Required

**Feasibility Assessment:**

| Component | Frida Alone | Frida + Static Tools | Static Tools Only |
|-----------|-------------|---------------------|-------------------|
| Function Discovery | ✅ Excellent | ✅ Excellent | ⚠️ Moderate |
| Call Graph | ✅ Excellent | ✅ Excellent | ⚠️ Moderate |
| Signature Inference | ⚠️ Partial | ✅ Good | ❌ Poor |
| Class/VTable Recovery | ⚠️ Partial | ✅ Good | ⚠️ Moderate |
| RTTI Reconstruction | ⚠️ Partial | ✅ Good | ⚠️ Moderate |
| File Structure Mapping | ❌ Poor | ⚠️ Partial | ❌ Poor |
| **Overall** | **60%** | **85%** | **40%** |

**Key Finding:** Frida excels at **dynamic discovery** (what runs, when, how) but requires **static analysis** for **structural recovery** (class hierarchies, namespaces, original organization). A hybrid pipeline combining both is optimal.

---

## 1. Feasibility Analysis

### 1.1 What Frida Can Do Exceptionally Well

#### ✅ Function Discovery via Execution Tracing

**Frida Stalker** can trace every executed instruction, discovering functions as they execute:

```javascript
// Discover all executed functions
Stalker.follow(pid, {
    events: {
        call: true,      // Function calls
        ret: true,       // Returns
        exec: true       // Instruction execution
    },
    onReceive: function(events) {
        // Build function call graph from execution trace
    }
});
```

**Advantages:**
- Discovers **only executed code** (no dead code noise)
- Captures **runtime call patterns** (real execution flow)
- Works with **stripped binaries** (no symbols needed)
- Handles **polymorphic calls** (virtual function dispatch)

**Limitations:**
- Requires **code coverage** (unexecuted functions remain unknown)
- **Performance overhead** (10-100x slowdown with full tracing)
- May miss **cold paths** (error handlers, initialization)

#### ✅ Call Graph Construction

Frida can build accurate call graphs by intercepting:
- Direct calls (`BL` instructions)
- Indirect calls (function pointers, vtables)
- Returns (`RET` instructions)

```javascript
// Hook all function calls
Interceptor.attach(Module.findBaseAddress("Cyberpunk2077"), {
    onEnter: function(args) {
        const caller = this.returnAddress;
        const callee = this.context.pc;
        callGraph.addEdge(caller, callee);
    }
});
```

**Success Rate:** ~95% for executed code paths

#### ✅ Signature Inference via Argument Analysis

By hooking functions and analyzing register/stack usage:

```javascript
Interceptor.attach(targetFunc, {
    onEnter: function(args) {
        // ARM64: X0-X7 = arguments, X8 = indirect result
        const argCount = this.inferArgCount();
        const argTypes = this.inferArgTypes(args);
        signatures[targetFunc] = {
            args: argCount,
            types: argTypes,
            returnType: this.inferReturnType()
        };
    }
});
```

**Heuristics:**
- **Argument count:** Count non-zero registers X0-X7 before call
- **Argument types:** Pointer vs integer (check address validity)
- **Return type:** Analyze return value usage
- **Calling convention:** ARM64 AAPCS64 standard

**Accuracy:** ~70% for simple functions, ~40% for complex C++

#### ⚠️ Partial: Class/VTable Recovery

**What Works:**
- **VTable discovery:** Hook virtual calls, trace `this` pointer to vtable
- **Object layout:** Analyze member access patterns (LDR from `this+offset`)
- **Inheritance:** Trace parent class pointers in object headers

**What's Hard:**
- **Class names:** Require RTTI or string analysis
- **Method names:** Require symbols or manual naming
- **Complete hierarchy:** Need static analysis of vtable structures

**Example:**
```javascript
// Discover vtable by hooking virtual calls
Interceptor.attach(virtualCallSite, {
    onEnter: function(args) {
        const thisPtr = args[0];
        const vtable = thisPtr.readPointer();
        vtables.add(vtable);
        
        // Analyze vtable entries
        for (let i = 0; i < 20; i++) {
            const method = vtable.add(i * 8).readPointer();
            if (isValidFunction(method)) {
                vtableMethods.add({vtable, index: i, method});
            }
        }
    }
});
```

#### ⚠️ Partial: RTTI Reconstruction

**Current State:** Cyberpunk 2077 has RTTI (see `CYBERPUNK_INTERNALS.md`)

**Frida Approach:**
1. **Find RTTI structures:** Hook `typeid()` calls, trace RTTI metadata
2. **Extract type names:** Read `CName` hashes, reverse FNV1a64
3. **Build class hierarchy:** Trace `CClass::parent` pointers

**Limitations:**
- **Hash collisions:** FNV1a64 hashes may collide
- **Incomplete coverage:** Only types used at runtime
- **Name recovery:** Requires hash database or brute force

**Hybrid Solution:**
```javascript
// 1. Frida: Discover RTTI usage
const rttiTypes = new Set();
Interceptor.attach(rttiSystemGet, {
    onEnter: function(args) {
        const typeHash = args[0].toInt32();
        rttiTypes.add(typeHash);
    }
});

// 2. Static: Extract RTTI structures from __DATA_CONST
// 3. Combine: Map runtime usage to static structures
```

### 1.2 What Requires Static Analysis

#### ❌ File Structure Mapping

**Problem:** Original source file organization is **lost** during compilation.

**Frida Limitation:** Can't determine which functions came from which `.cpp` files.

**Partial Solutions:**
1. **Symbol prefixes:** If symbols exist, demangle to get namespace hints
2. **Function clustering:** Group functions by memory proximity (±64KB)
3. **String domain analysis:** Functions using same strings likely same file
4. **Import clustering:** Functions importing same symbols

**Example Mapping Strategy:**
```python
# Map functions to pseudo-modules
modules = {
    'TweakDB': functions_in_range(0x2B73xxx, 0x2B7Dxxx),
    'RTTI': functions_in_range(0x4D3xxx, 0x4D5xxx),
    'Stats': functions_in_range(0x3A93xxx, 0x3A94xxx),
}
```

#### ❌ Complete Type Recovery

**Problem:** C++ types are erased; only memory layout remains.

**Static Tools Needed:**
- **Ghidra/IDA:** Structure recovery from memory access patterns
- **RTTI parsers:** Extract complete type information
- **VTable analyzers:** Reconstruct inheritance hierarchies

---

## 2. Proposed Pipeline Design

### Phase 1: Module & Symbol Discovery (Static + Dynamic)

**Tools:** `otool`, `nm`, Frida `Module.enumerateSymbols()`

```javascript
// 1. Enumerate all modules
const modules = Process.enumerateModules();
const symbols = {};

for (const mod of modules) {
    // 2. Extract exported symbols
    const exports = Module.enumerateExports(mod.name);
    
    // 3. Extract symbol table (if not stripped)
    const syms = Module.enumerateSymbols(mod.name);
    
    // 4. Demangle C++ names
    for (const sym of syms) {
        symbols[sym.address] = {
            name: sym.name,
            demangled: demangle(sym.name),
            module: mod.name
        };
    }
}
```

**Output:** Symbol database mapping addresses → names

### Phase 2: Function Discovery (Hybrid)

**2.1 Static Function Discovery**

```python
# scripts/re_tools/function_discovery.py
def discover_functions_static(binary_path):
    """Find function boundaries via prologue patterns."""
    functions = []
    
    # ARM64 prologues:
    # STP X29, X30, [SP, #-0x??]!  (0xFD 0x7B ?? 0xA9)
    # SUB SP, SP, #0x??            (0xFF 0x83 0x?? 0xD1)
    
    with open(binary_path, 'rb') as f:
        data = f.read()
        for offset in find_prologues(data):
            functions.append({
                'offset': offset,
                'address': IMAGE_BASE + offset,
                'prologue_type': detect_prologue_type(data[offset:offset+4])
            })
    
    return functions
```

**2.2 Dynamic Function Discovery**

```javascript
// Frida: Trace execution to discover executed functions
const executedFunctions = new Set();

Stalker.follow(pid, {
    events: {
        call: true,
        ret: true
    },
    onReceive: function(events) {
        for (const event of events) {
            if (event.type === 'call') {
                executedFunctions.add(event.address);
            }
        }
    }
});

// Run game through various scenarios:
// - Main menu
// - Loading screen
// - In-game
// - RT features enabled
```

**2.3 Combine Results**

```python
# Merge static + dynamic discovery
all_functions = set(static_functions) | set(dynamic_functions)

# Prioritize dynamic (executed) functions
function_confidence = {
    func: 'high' if func in dynamic_functions else 'low'
    for func in all_functions
}
```

**Output:** Complete function list with confidence scores

### Phase 3: Call Graph Construction (Frida)

**3.1 Direct Call Tracing**

```javascript
// Hook all BL (branch with link) instructions
const callGraph = new Map(); // caller → [callees]

Stalker.follow(pid, {
    transform: function(iterator) {
        iterator.putCallout(function(context) {
            const pc = context.pc;
            const lr = context.lr; // Link register = caller
            
            if (isInGameModule(pc) && isInGameModule(lr)) {
                const caller = lr;
                const callee = pc;
                
                if (!callGraph.has(caller)) {
                    callGraph.set(caller, []);
                }
                callGraph.get(caller).push(callee);
            }
        });
        
        iterator.keep();
    }
});
```

**3.2 Indirect Call Analysis**

```javascript
// Hook virtual function calls
Interceptor.attach(vtableAccessSite, {
    onEnter: function(args) {
        const thisPtr = args[0];
        const vtable = thisPtr.readPointer();
        const methodIndex = inferVTableIndex(this.context);
        const method = vtable.add(methodIndex * 8).readPointer();
        
        callGraph.addVirtualCall(this.returnAddress, method, {
            vtable: vtable,
            index: methodIndex
        });
    }
});
```

**3.3 String-Based XRef Discovery**

```javascript
// Find functions referencing specific strings
const stringXRefs = new Map();

function findStringReferences(targetString) {
    const stringAddr = findStringAddress(targetString);
    
    // Scan for ADRP+ADD referencing string
    Memory.scan(Module.findBaseAddress("Cyberpunk2077"), 
                Module.findBaseAddress("Cyberpunk2077").add(0x10000000),
                "?? ?? ?? ?? ?? ?? ?? ??", // ADRP pattern
                {
                    onMatch: function(address, size) {
                        // Check if this ADRP references stringAddr
                        if (referencesString(address, stringAddr)) {
                            const func = findContainingFunction(address);
                            stringXRefs.set(func, targetString);
                        }
                    }
                });
}
```

**Output:** Complete call graph (direct + indirect + string-based)

### Phase 4: Signature Inference (Frida + Heuristics)

**4.1 Argument Count Detection**

```javascript
function inferArgumentCount(funcAddr) {
    const samples = [];
    
    Interceptor.attach(funcAddr, {
        onEnter: function(args) {
            // Count non-zero arguments (heuristic)
            let argCount = 0;
            for (let i = 0; i < 8; i++) {
                if (args[i].toInt32() !== 0 || 
                    isValidPointer(args[i])) {
                    argCount = Math.max(argCount, i + 1);
                }
            }
            samples.push(argCount);
        }
    });
    
    // Run function multiple times, take mode
    return mode(samples);
}
```

**4.2 Type Inference**

```javascript
function inferArgumentTypes(funcAddr) {
    const typeSamples = [];
    
    Interceptor.attach(funcAddr, {
        onEnter: function(args) {
            const types = [];
            for (let i = 0; i < 8; i++) {
                const val = args[i];
                
                // Heuristics:
                if (isValidPointer(val)) {
                    types.push('pointer');
                } else if (val.toInt32() < 256) {
                    types.push('int32'); // or uint8
                } else {
                    types.push('int64');
                }
            }
            typeSamples.push(types);
        }
    });
    
    return aggregateTypes(typeSamples);
}
```

**4.3 Return Type Inference**

```javascript
Interceptor.attach(funcAddr, {
    onLeave: function(retval) {
        // Analyze how return value is used
        const returnAddr = this.returnAddress;
        
        // Check if caller uses return value as pointer
        const usage = analyzeReturnUsage(returnAddr);
        returnTypes[funcAddr] = usage.type;
    }
});
```

**Output:** Function signatures (best-effort)

### Phase 5: Class/VTable Recovery (Hybrid)

**5.1 VTable Discovery (Frida)**

```javascript
// Strategy 1: Hook virtual calls
const vtables = new Map(); // vtable_addr → methods[]

Interceptor.attachModule("Cyberpunk2077", {
    onEnter: function(details) {
        // Find virtual call sites (LDR X?, [X0]; BLR X?)
        if (isVirtualCallSite(details.address)) {
            const thisPtr = this.context.x0;
            const vtable = thisPtr.readPointer();
            
            if (!vtables.has(vtable)) {
                vtables.set(vtable, []);
            }
            
            const methodIndex = inferVTableIndex(details.address);
            const method = vtable.add(methodIndex * 8).readPointer();
            
            vtables.get(vtable).push({
                index: methodIndex,
                method: method
            });
        }
    }
});
```

**5.2 RTTI Correlation (Static + Dynamic)**

```python
# scripts/re_tools/rtti_recovery.py
def correlate_vtables_with_rtti(vtables, rtti_data):
    """Map vtables to RTTI type information."""
    class_hierarchy = {}
    
    for vtable_addr, methods in vtables.items():
        # Find RTTI structure referencing this vtable
        rtti_type = find_rtti_for_vtable(vtable_addr, rtti_data)
        
        if rtti_type:
            class_hierarchy[vtable_addr] = {
                'name': rtti_type.name,
                'parent': rtti_type.parent,
                'methods': methods
            }
    
    return class_hierarchy
```

**5.3 Object Layout Inference**

```javascript
// Analyze member access patterns
function inferObjectLayout(classFuncs) {
    const offsets = new Set();
    
    for (const func of classFuncs) {
        // Scan for LDR X?, [X0, #offset]
        const memberAccesses = findMemberAccesses(func);
        for (const offset of memberAccesses) {
            offsets.add(offset);
        }
    }
    
    return Array.from(offsets).sort((a, b) => a - b);
}
```

**Output:** Class hierarchy with vtables and member layouts

### Phase 6: File Structure Approximation

**6.1 Function Clustering**

```python
def cluster_functions_by_proximity(functions, cluster_size=0x10000):
    """Group functions by memory proximity."""
    clusters = {}
    
    for func in functions:
        cluster_key = func.address // cluster_size
        if cluster_key not in clusters:
            clusters[cluster_key] = []
        clusters[cluster_key].append(func)
    
    return clusters
```

**6.2 String Domain Analysis**

```python
def cluster_by_string_domain(functions, string_xrefs):
    """Group functions using same strings."""
    string_domains = {}
    
    for func, strings in string_xrefs.items():
        # Create domain signature from string set
        domain_key = hash(tuple(sorted(strings)))
        
        if domain_key not in string_domains:
            string_domains[domain_key] = {
                'strings': strings,
                'functions': []
            }
        string_domains[domain_key]['functions'].append(func)
    
    return string_domains
```

**6.3 Module Taxonomy**

```python
def create_module_taxonomy(functions, call_graph, string_xrefs):
    """Create pseudo-module structure."""
    modules = {
        'TweakDB': {
            'address_range': (0x2B73xxx, 0x2B7Dxxx),
            'functions': [],
            'strings': ['.tweak', 'TweakDB', 'LoadOptimized']
        },
        'RTTI': {
            'address_range': (0x4D3xxx, 0x4D5xxx),
            'functions': [],
            'strings': ['CClass', 'CRTTISystem']
        },
        # ... more modules
    }
    
    # Assign functions to modules
    for func in functions:
        module = find_matching_module(func, modules)
        if module:
            module['functions'].append(func)
    
    return modules
```

**Output:** Pseudo-file structure organized by subsystem

---

## 3. Deliverables

### 3.1 JSON Database Schema

```json
{
  "version": "1.0",
  "game_version": "2.21",
  "timestamp": "2026-01-XX",
  "functions": {
    "0x2B79AC0": {
      "name": "TweakDB_Init",
      "offset": "0x2B79AC0",
      "size": 256,
      "confidence": "high",
      "discovery_method": "dynamic",
      "signature": {
        "args": 1,
        "arg_types": ["TweakDB*"],
        "return_type": "void",
        "confidence": 0.8
      },
      "callers": ["0x31E18"],
      "callees": ["0x2B737AC", "0x2B7BE94"],
      "module": "TweakDB",
      "strings": [".tweak", "TweakDB"],
      "vtables": null,
      "member_accesses": []
    }
  },
  "classes": {
    "TweakDB": {
      "vtable": "0x...",
      "methods": [
        {"index": 0, "offset": "0x2B79AC0", "name": "Init"},
        {"index": 1, "offset": "0x2B7BE94", "name": "Load"}
      ],
      "parent": null,
      "members": [
        {"offset": 0x0, "type": "vtable*"},
        {"offset": 0x8, "type": "unknown"}
      ]
    }
  },
  "call_graph": {
    "edges": [
      {"caller": "0x31E18", "callee": "0x2B79AC0", "type": "direct"},
      {"caller": "0x2B79AC0", "callee": "0x2B737AC", "type": "direct"}
    ]
  },
  "modules": {
    "TweakDB": {
      "address_range": ["0x2B73xxx", "0x2B7Dxxx"],
      "functions": ["0x2B79AC0", "..."],
      "strings": [".tweak", "TweakDB"]
    }
  }
}
```

### 3.2 Incremental Update Scripts

```python
# scripts/re_tools/incremental_update.py
def update_addresses(old_db, new_binary):
    """Update function addresses between game versions."""
    # Strategy:
    # 1. Find anchor functions (unique string references)
    # 2. Re-locate anchors in new binary
    # 3. Apply relative offsets from old DB
    
    anchor_functions = find_anchor_functions(old_db)
    new_anchors = locate_anchors(new_binary, anchor_functions)
    
    offset_delta = compute_offset_delta(anchor_functions, new_anchors)
    
    updated_db = apply_offset_delta(old_db, offset_delta)
    return updated_db
```

### 3.3 Navigation Tools

**IDAPython/Ghidra Scripts:**
- Import JSON database
- Apply function names
- Create structures from class layouts
- Generate call graph visualization

**VS Code Extension:**
- Navigate functions by name/hash
- Jump to callers/callees
- View signatures and types

---

## 4. Risks & Limitations

### 4.1 Technical Risks

| Risk | Impact | Mitigation |
|------|--------|------------|
| **Anti-debugging** | High | Use Frida's stealth mode; hook anti-debug checks |
| **ASLR** | Low | Frida handles ASLR; use relative offsets in DB |
| **Performance Overhead** | Medium | Use selective tracing; profile specific subsystems |
| **Code Coverage** | Medium | Run multiple scenarios; combine with static analysis |
| **Hash Collisions** | Low | Use additional context (string refs, call patterns) |

### 4.2 Completeness Limits

**What We Can't Recover:**
- ❌ Original source file names (lost during compilation)
- ❌ Comments and documentation
- ❌ Variable names (only types/layouts)
- ❌ Unused/dead code paths
- ❌ Exact original code structure

**What We Can Recover:**
- ✅ Function boundaries and addresses
- ✅ Call graphs (executed paths)
- ✅ Approximate signatures
- ✅ Class hierarchies (with RTTI)
- ✅ VTable layouts
- ✅ String references and XRefs
- ✅ Pseudo-module organization

### 4.3 Legal/ToS Considerations

**High-Level Assessment:**
- ✅ **Reverse engineering for interoperability** is generally legal (DMCA exemption)
- ✅ **Modding/hooking** is typically allowed by game ToS
- ⚠️ **Redistribution of decompiled code** may violate copyright
- ⚠️ **Commercial use** of reverse-engineered information may have restrictions

**Recommendation:**
- Focus on **structure/metadata**, not code decompilation
- Generate **abstract representations** (signatures, graphs), not source code
- Use for **modding/hooking purposes** only
- Document **discovery methodology**, not game internals

---

## 5. Minimal Prototype Plan

### Phase 1: Proof of Concept (2-3 weeks)

**Goal:** Demonstrate Frida can discover and map 50+ functions with call graph

**Deliverables:**
1. **Function Discovery Script** (`scripts/re_tools/frida_function_discovery.js`)
   - Use Stalker to trace execution
   - Extract function boundaries
   - Build call graph

2. **Signature Inference** (`scripts/re_tools/frida_signature_inference.js`)
   - Hook 10 known functions (TweakDB, RTTI)
   - Infer argument counts/types
   - Compare with known signatures

3. **JSON Output** (`cyberpunk2077_functions.json`)
   - 50+ functions with addresses
   - Call graph edges
   - Basic signatures

**Success Criteria:**
- ✅ Discover 50+ functions automatically
- ✅ Build call graph with 100+ edges
- ✅ Infer signatures with 60%+ accuracy for simple functions
- ✅ Output navigable JSON database

### Phase 2: Enhanced Discovery (3-4 weeks)

**Goal:** Add class/vtable recovery and RTTI correlation

**Deliverables:**
1. **VTable Discovery** (`scripts/re_tools/frida_vtable_recovery.js`)
2. **RTTI Parser** (`scripts/re_tools/rtti_parser.py`)
3. **Class Hierarchy** (`cyberpunk2077_classes.json`)

**Success Criteria:**
- ✅ Recover 20+ class vtables
- ✅ Map vtables to RTTI types
- ✅ Infer object layouts for 10+ classes

### Phase 3: File Structure Mapping (2-3 weeks)

**Goal:** Create pseudo-module organization

**Deliverables:**
1. **Function Clustering** (`scripts/re_tools/function_clustering.py`)
2. **Module Taxonomy** (`cyberpunk2077_modules.json`)
3. **Navigation Tools** (IDAPython scripts)

**Success Criteria:**
- ✅ Organize functions into 10+ logical modules
- ✅ Generate navigable structure
- ✅ Enable IDA/Ghidra import

---

## 6. Required Tools & Libraries

### Core Tools

| Tool | Purpose | License | When to Use |
|------|---------|---------|-------------|
| **Frida** | Dynamic instrumentation | LGPL | Function discovery, call graphs, runtime tracing |
| **frida-tools** | CLI utilities | LGPL | Scripting Frida operations |
| **frida-gum** | Low-level hooking | LGPL | In-process hooking |
| **otool** | Mach-O analysis | Apple (included) | **Quick parsing, disassembly, strings** |
| **nm** | Symbol extraction | Apple (included) | Symbol table extraction |

### Static Analysis (Hybrid)

| Tool | Purpose | License | When to Use |
|------|---------|---------|-------------|
| **Ghidra** | Structure recovery, RTTI parsing | Apache 2.0 | **RTTI extraction, structure recovery, deep analysis** |
| **IDA Pro** | Advanced analysis (optional) | Commercial | Optional, if available |
| **rizin** | Open-source alternative | LGPL | Alternative to Ghidra |

### Tool Selection Strategy

**Use `otool` for:**
- ✅ Fast segment/load command parsing (already integrated)
- ✅ Quick disassembly of specific functions
- ✅ String extraction and searching
- ✅ Basic symbol listing
- ✅ Script automation (lightweight, fast)

**Use `Ghidra headless` for:**
- ✅ RTTI structure extraction (complete type information)
- ✅ Structure recovery from memory access patterns
- ✅ Complete binary coverage (not just executed code)
- ✅ Complex analysis scripts (export data, find patterns)
- ✅ VTable reconstruction and class hierarchy building

**Use `Frida` for:**
- ✅ Dynamic function discovery (execution tracing)
- ✅ Call graph construction (runtime behavior)
- ✅ Signature inference (argument analysis)
- ✅ VTable discovery via virtual calls

**Recommended Workflow:**
1. **otool** → Quick initial parsing, string discovery, basic disassembly
2. **Frida** → Dynamic discovery, call graphs, runtime analysis
3. **Ghidra headless** → Deep static analysis, RTTI extraction, structure recovery
4. **Combine results** → Merge dynamic + static findings into unified database

### Python Libraries

```python
# requirements-re.txt
frida>=16.0.0
frida-tools>=12.0.0
capstone>=5.0.0      # Disassembly
r2pipe>=1.7.0        # Rizin integration (optional)
networkx>=3.0        # Call graph analysis
```

---

## 7. Implementation Roadmap

### Week 1-2: Foundation
- [ ] Set up Frida environment
- [ ] Create function discovery script
- [ ] Test on known functions (TweakDB)

### Week 3-4: Call Graph
- [ ] Implement Stalker-based tracing
- [ ] Build call graph database
- [ ] Add indirect call handling

### Week 5-6: Signatures
- [ ] Implement argument inference
- [ ] Add type heuristics
- [ ] Validate against known signatures

### Week 7-8: Classes/VTables
- [ ] VTable discovery via virtual calls
- [ ] RTTI correlation
- [ ] Object layout inference

### Week 9-10: Organization
- [ ] Function clustering
- [ ] Module taxonomy
- [ ] JSON schema finalization

### Week 11-12: Tools & Integration
- [ ] IDAPython import scripts
- [ ] Navigation tools
- [ ] Documentation

---

## 8. Conclusion

**Frida is highly effective for dynamic reverse engineering** but requires **static analysis** for complete structural recovery. A **hybrid pipeline** combining:

1. **Frida** for execution tracing, call graphs, signature inference
2. **Static tools** (Ghidra/otool) for RTTI, structures, complete coverage
3. **Heuristics** for file structure approximation

...can produce a **navigable, actionable code representation** suitable for modding and hooking.

**Expected Outcome:**
- ✅ 500+ functions discovered and mapped
- ✅ Complete call graph (executed paths)
- ✅ 50+ classes with vtables
- ✅ Navigable JSON database
- ✅ Incremental update capability

**Timeline:** 10-12 weeks for full implementation

**Recommendation:** **Proceed with hybrid approach**, starting with Frida-based dynamic discovery, then enhancing with static analysis for completeness.

---

## Appendix: Example Frida Scripts

### A.1 Function Discovery

```javascript
// scripts/re_tools/frida_function_discovery.js
const discoveredFunctions = new Map();
const callGraph = new Map();

Stalker.follow(Process.id, {
    events: {
        call: true,
        ret: true
    },
    onReceive: function(events) {
        for (const event of events) {
            if (event.type === 'call') {
                const func = event.address;
                const caller = event.returnAddress;
                
                if (!discoveredFunctions.has(func)) {
                    discoveredFunctions.set(func, {
                        address: func,
                        offset: func.sub(Module.findBaseAddress("Cyberpunk2077")),
                        callers: [],
                        callees: [],
                        callCount: 0
                    });
                }
                
                const funcInfo = discoveredFunctions.get(func);
                funcInfo.callCount++;
                
                if (!callGraph.has(caller)) {
                    callGraph.set(caller, []);
                }
                callGraph.get(caller).push(func);
            }
        }
    }
});

// Export results
rpc.exports = {
    getFunctions: () => Array.from(discoveredFunctions.values()),
    getCallGraph: () => Object.fromEntries(callGraph)
};
```

### A.2 Signature Inference

```javascript
// scripts/re_tools/frida_signature_inference.js
const signatures = new Map();

function inferSignature(funcAddr) {
    const argSamples = [];
    const returnSamples = [];
    
    Interceptor.attach(funcAddr, {
        onEnter: function(args) {
            // Sample arguments
            const args_sample = [];
            for (let i = 0; i < 8; i++) {
                const val = args[i];
                args_sample.push({
                    isPointer: isValidPointer(val),
                    value: val.toInt32()
                });
            }
            argSamples.push(args_sample);
        },
        onLeave: function(retval) {
            returnSamples.push({
                isPointer: isValidPointer(retval),
                value: retval.toInt32()
            });
        }
    });
    
    // Analyze samples after collection
    return {
        argCount: inferArgCount(argSamples),
        argTypes: inferArgTypes(argSamples),
        returnType: inferReturnType(returnSamples)
    };
}
```

### A.3 VTable Discovery

```javascript
// scripts/re_tools/frida_vtable_discovery.js
const vtables = new Map();

// Hook virtual call pattern: LDR X?, [X0]; BLR X?
Memory.scan(Module.findBaseAddress("Cyberpunk2077"),
            Module.findBaseAddress("Cyberpunk2077").add(0x10000000),
            "?? ?? 40 F9 ?? ?? 1F D6", // LDR + BLR pattern
            {
                onMatch: function(address, size) {
                    Interceptor.attach(address, {
                        onEnter: function(args) {
                            const thisPtr = args[0];
                            const vtable = thisPtr.readPointer();
                            
                            if (!vtables.has(vtable)) {
                                vtables.set(vtable, {
                                    address: vtable,
                                    methods: [],
                                    callSites: []
                                });
                            }
                            
                            // Infer method index from instruction
                            const methodIndex = inferVTableIndex(address);
                            const method = vtable.add(methodIndex * 8).readPointer();
                            
                            vtables.get(vtable).methods.push({
                                index: methodIndex,
                                address: method
                            });
                        }
                    });
                }
            });
```

---

## Appendix B: Practical Tool Usage Examples

### B.1 Using otool for Quick Analysis

```bash
# Extract all strings (fast)
otool -s __TEXT __cstring Cyberpunk2077 > strings.txt

# Disassemble specific function range
otool -tV Cyberpunk2077 | sed -n '/0x2B79AC0/,/^$/p'

# List all segments (for address calculation)
otool -l Cyberpunk2077 | grep -A 5 "segname __TEXT"

# Find imports (for dependency analysis)
otool -L Cyberpunk2077
```

**Python integration:**
```python
# scripts/re_tools/otool_parser.py
import subprocess
import re

def extract_strings(binary_path):
    """Fast string extraction using otool."""
    result = subprocess.run(
        ['otool', '-s', '__TEXT', '__cstring', binary_path],
        capture_output=True, text=True
    )
    # Parse otool output...
    return strings

def disassemble_range(binary_path, start_offset, end_offset):
    """Quick disassembly of address range."""
    result = subprocess.run(
        ['otool', '-tV', binary_path],
        capture_output=True, text=True
    )
    # Filter by address range...
    return instructions
```

### B.2 Using Ghidra Headless for Deep Analysis

```bash
# Setup: Download Ghidra and set path
export GHIDRA_HOME=/path/to/ghidra_11.0
export PATH=$GHIDRA_HOME/support:$PATH

# Extract RTTI structures
analyzeHeadless /tmp/ghidra_proj cp2077_re \
  -import Cyberpunk2077 \
  -processor AARCH64:LE:64:v8A \
  -cspec default \
  -postScript ExtractRTTI.py rtti_output.json \
  -deleteProject

# Recover structures from memory patterns
analyzeHeadless /tmp/ghidra_proj cp2077_re \
  -process Cyberpunk2077 \
  -postScript RecoverStructures.py structures.json \
  -deleteProject

# Export function information
analyzeHeadless /tmp/ghidra_proj cp2077_re \
  -process Cyberpunk2077 \
  -postScript ExportFunctions.py functions.json \
  -deleteProject
```

**Ghidra Script Example (ExtractRTTI.py):**
```python
# Ghidra script to extract RTTI information
from ghidra.program.model.listing import FunctionManager
from ghidra.program.model.symbol import SymbolTable
import json

def extract_rtti():
    """Extract RTTI type information from binary."""
    rtti_types = []
    
    # Find RTTI structures in __DATA_CONST
    data_const = getMemoryBlock("__DATA_CONST")
    if data_const:
        # Scan for RTTI metadata...
        # Extract type names, vtables, inheritance
        pass
    
    return rtti_types

if __name__ == "__main__":
    rtti_data = extract_rtti()
    with open("rtti_output.json", "w") as f:
        json.dump(rtti_data, f, indent=2)
```

### B.3 Combined Pipeline Example

```python
# scripts/re_tools/hybrid_analysis.py
"""
Combined otool + Ghidra + Frida analysis pipeline.
"""

import subprocess
import json
from pathlib import Path

def phase1_otool_analysis(binary_path):
    """Phase 1: Quick otool-based parsing."""
    print("[*] Phase 1: otool analysis...")
    
    # Extract strings
    strings = extract_strings_otool(binary_path)
    
    # Parse segments
    segments = parse_segments_otool(binary_path)
    
    # Basic disassembly
    functions = find_functions_otool(binary_path)
    
    return {
        'strings': strings,
        'segments': segments,
        'functions': functions
    }

def phase2_frida_analysis(binary_path):
    """Phase 2: Frida dynamic discovery."""
    print("[*] Phase 2: Frida dynamic analysis...")
    
    # Run Frida script
    result = subprocess.run(
        ['frida', '-l', 'frida_function_discovery.js', '-f', binary_path],
        capture_output=True, text=True
    )
    
    # Parse Frida output
    frida_data = json.loads(result.stdout)
    
    return {
        'executed_functions': frida_data['functions'],
        'call_graph': frida_data['call_graph']
    }

def phase3_ghidra_analysis(binary_path, ghidra_home):
    """Phase 3: Ghidra deep static analysis."""
    print("[*] Phase 3: Ghidra analysis...")
    
    # Run Ghidra headless
    analyze_headless = Path(ghidra_home) / "support" / "analyzeHeadless"
    
    subprocess.run([
        str(analyze_headless),
        "/tmp/ghidra_proj", "cp2077",
        "-import", str(binary_path),
        "-processor", "AARCH64:LE:64:v8A",
        "-postScript", "ExtractRTTI.py", "rtti.json",
        "-deleteProject"
    ])
    
    # Load Ghidra results
    with open("rtti.json") as f:
        rtti_data = json.load(f)
    
    return {
        'rtti': rtti_data,
        'structures': load_structures()
    }

def combine_results(otool_data, frida_data, ghidra_data):
    """Combine all analysis results."""
    print("[*] Combining results...")
    
    # Merge function lists (prioritize Frida for executed functions)
    all_functions = {}
    
    # Add otool functions (low confidence)
    for func in otool_data['functions']:
        all_functions[func['address']] = {
            **func,
            'confidence': 'low',
            'discovery_method': 'static'
        }
    
    # Add Frida functions (high confidence)
    for func in frida_data['executed_functions']:
        addr = func['address']
        if addr in all_functions:
            all_functions[addr]['confidence'] = 'high'
            all_functions[addr]['discovery_method'] = 'dynamic'
            all_functions[addr]['call_count'] = func.get('call_count', 0)
        else:
            all_functions[addr] = {
                **func,
                'confidence': 'high',
                'discovery_method': 'dynamic'
            }
    
    # Add Ghidra RTTI information
    for class_info in ghidra_data['rtti']:
        # Correlate with functions...
        pass
    
    return {
        'functions': all_functions,
        'call_graph': frida_data['call_graph'],
        'rtti': ghidra_data['rtti'],
        'strings': otool_data['strings']
    }

def main():
    binary_path = Path("Cyberpunk2077")
    ghidra_home = os.environ.get("GHIDRA_HOME", "/opt/ghidra")
    
    # Run all phases
    otool_data = phase1_otool_analysis(binary_path)
    frida_data = phase2_frida_analysis(binary_path)
    ghidra_data = phase3_ghidra_analysis(binary_path, ghidra_home)
    
    # Combine
    combined = combine_results(otool_data, frida_data, ghidra_data)
    
    # Save
    with open("cyberpunk2077_analysis.json", "w") as f:
        json.dump(combined, f, indent=2)
    
    print("[+] Analysis complete!")

if __name__ == "__main__":
    main()
```

### B.4 When to Use Which Tool - Decision Tree

```
Need to analyze binary?
│
├─ Quick parsing needed? (segments, strings, basic disassembly)
│  └─> Use otool (fast, lightweight)
│
├─ Runtime behavior needed? (what executes, call patterns)
│  └─> Use Frida (dynamic discovery)
│
├─ Deep analysis needed? (RTTI, structures, complete coverage)
│  └─> Use Ghidra headless (comprehensive static analysis)
│
└─ Complete picture needed?
   └─> Use ALL THREE:
       1. otool → Quick initial parsing
       2. Frida → Dynamic discovery
       3. Ghidra → Deep static analysis
       4. Combine → Unified database
```

---

**Document Version:** 1.1  
**Last Updated:** January 2026  
**Author:** RED4ext macOS Port Team
