/**
 * Phase B: Frida RTTI Dumper for Cyberpunk 2077 macOS
 *
 * Hooks CRTTISystem::Get after RTTI registration completes, walks the
 * internal HashMap of types and global functions, and dumps a complete
 * class/function/property database with native addresses.
 *
 * Usage:
 *   frida -D local -n Cyberpunk2077 -l rtti_dumper.js
 *   -- OR --
 *   Copy alongside FridaGadget.config and point config at this script.
 *
 * Output: /tmp/rtti_dump.json
 */

'use strict';

const IMAGE_BASE = ptr('0x100000000');

// CRTTISystem::Get offset (from cyberpunk2077_addresses.json, hash 0x4A610F64)
const RTTI_SYSTEM_GET_OFFSET = 0x3452734;

// CBaseEngine::LoadScripts (we hook this to know when RTTI is ready)
const LOAD_SCRIPTS_OFFSET = 0x3D9A03C;

// ---------- struct offsets ----------

const RTTI = {
    types:  0x10,   // HashMap<CName, CBaseRTTIType*>
    funcs:  0xA0,   // HashMap<CName, CGlobalFunction*>
};

const HASHMAP = {
    indexTable: 0x00,
    size:       0x08,
    capacity:   0x0C,
    nodeList:   0x10,
};

const NODELIST = {
    nodes:    0x00,
    capacity: 0x08,
    stride:   0x0C,
    nextIdx:  0x10,
    size:     0x14,
};

// Nodes in HashMap<CName, T*>
const NODE = {
    next:      0x00, // uint32_t
    hashedKey: 0x04, // uint32_t
    key:       0x08, // CName (uint64_t)
    value:     0x10, // T*
};

const CCLASS = {
    parent: 0x10,
    name:   0x18,
    funcs:  0x48,       // DynArray<CClassFunction*>
    staticFuncs: 0x58,  // DynArray<CClassStaticFunction*>
    props:  0x28,       // DynArray<CProperty*>
    size_:  0x68,       // uint32_t
    flags:  0x70,
};

const CBASEFUNC = {
    fullName:  0x08,
    shortName: 0x10,
    returnType: 0x18,
    params:    0x28,    // DynArray<CProperty*>
    flags:     0xA8,
};

const CPROPERTY = {
    name:   0x08,       // CName
    type:   0x00,       // CBaseRTTIType*
    offset: 0x18,       // uint32_t (offset in parent class)
};

const DYNARRAY = {
    entries: 0x00,
    capacity: 0x08,
    size:     0x0C,
};

// ---------- helpers ----------

function readCName(addr) {
    if (addr.isNull()) return '<null>';
    var raw = addr.readU64();
    if (raw.equals(uint64('0'))) return '<none>';
    // CName stores a hash; resolve via CNamePool at runtime.
    // Shortcut: read the debug string pointer at the CName object's address.
    // Actually, CName is just a uint64 hash. We need CNamePool to resolve.
    // For now, return the hash and resolve later.
    return raw.toString(16);
}

function readCNameStr(addr) {
    // CName is an interned uint64 hash. The engine stores the string in a pool.
    // We can resolve it by calling CName::ToString or reading the pool.
    // For a simpler approach: read the name from the CBaseRTTIType's internal
    // name field, which is often pointed to by a separate method.
    // Fallback: return hash.
    try {
        var hash = addr.readU64();
        if (hash.equals(uint64('0'))) return '';
        // Try to call a resolution function if available
        return 'hash_' + hash.toString(16);
    } catch(e) {
        return '<err>';
    }
}

function readDynArrayPtrs(base) {
    var entries = base.readPointer();
    var size = base.add(DYNARRAY.size).readU32();
    var result = [];
    if (entries.isNull() || size === 0) return result;
    for (var i = 0; i < size && i < 50000; i++) {
        result.push(entries.add(i * Process.pointerSize).readPointer());
    }
    return result;
}

function walkHashMap(base, callback) {
    var nodeListBase = base.add(HASHMAP.nodeList);
    var nodesPtr = nodeListBase.add(NODELIST.nodes).readPointer();
    var stride = nodeListBase.add(NODELIST.stride).readU32();
    var nodeCount = nodeListBase.add(NODELIST.size).readU32();

    if (nodesPtr.isNull() || stride === 0 || nodeCount === 0) {
        return 0;
    }

    var visited = 0;
    for (var i = 0; i < nodeCount && i < 200000; i++) {
        var nodeBase = nodesPtr.add(i * stride);
        var next = nodeBase.add(NODE.next).readU32();
        // 0xFFFFFFFF = unused slot
        if (next === 0xFFFFFFFF && i > 0) continue;
        var keyHash = nodeBase.add(NODE.key).readU64();
        if (keyHash.equals(uint64('0'))) continue;
        var value = nodeBase.add(NODE.value).readPointer();
        if (value.isNull()) continue;
        callback(keyHash, value);
        visited++;
    }
    return visited;
}

// ---------- CName resolution via pool ----------
// The CNamePool keeps an array of string pointers indexed by the lower bits.
// A simpler approach: hook CName's ToString or find the pool.
// Best approach for dumping: use CBaseRTTIType->GetName() virtual method.

function getTypeName(typePtr) {
    try {
        // CBaseRTTIType has a virtual method GetName() at vtable[1]
        // Let's just read the CName at the known offset and try to resolve
        var nameHash = typePtr.add(0x18).readU64(); // CClass.name offset
        // Try to use the vtable to call GetName
        var vtable = typePtr.readPointer();
        // GetName is usually vtable[1] (offset 0x08 in vtable)
        var getNameFn = vtable.add(0x08).readPointer();
        // It returns a CName (uint64) by value -- difficult to call from Frida
        // Alternative: read the name CName hash and look it up later
        return nameHash;
    } catch(e) {
        return uint64('0');
    }
}

// ---------- main dump logic ----------

var dumped = false;

function dumpRTTI() {
    if (dumped) return;
    dumped = true;

    send({type: 'log', msg: 'Starting RTTI dump...'});

    var getRTTI = new NativeFunction(
        IMAGE_BASE.add(RTTI_SYSTEM_GET_OFFSET),
        'pointer', []
    );

    var rttiSystem = getRTTI();
    if (rttiSystem.isNull()) {
        send({type: 'error', msg: 'CRTTISystem::Get returned null'});
        return;
    }

    send({type: 'log', msg: 'CRTTISystem at ' + rttiSystem});

    // ---- Walk types HashMap ----
    var typesBase = rttiSystem.add(RTTI.types);
    var types = {};
    var typeCount = 0;

    walkHashMap(typesBase, function(nameHash, typePtr) {
        typeCount++;
        var entry = {
            addr: typePtr.toString(),
            nameHash: nameHash.toString(16),
        };

        try {
            // Check if it's a CClass by reading the vtable
            var vtable = typePtr.readPointer();
            var parentPtr = typePtr.add(CCLASS.parent).readPointer();
            var classSize = typePtr.add(CCLASS.size_).readU32();

            // Read functions (DynArray<CClassFunction*>)
            var funcs = readDynArrayPtrs(typePtr.add(CCLASS.funcs));
            var staticFuncs = readDynArrayPtrs(typePtr.add(CCLASS.staticFuncs));
            var props = readDynArrayPtrs(typePtr.add(CCLASS.props));

            entry.parent = parentPtr.isNull() ? null : parentPtr.toString();
            entry.classSize = classSize;
            entry.funcCount = funcs.length;
            entry.staticFuncCount = staticFuncs.length;
            entry.propCount = props.length;

            // Dump function addresses
            entry.functions = [];
            var allFuncs = funcs.concat(staticFuncs);
            for (var j = 0; j < allFuncs.length && j < 500; j++) {
                var fptr = allFuncs[j];
                if (fptr.isNull()) continue;
                try {
                    var fnameHash = fptr.add(CBASEFUNC.fullName).readU64();
                    var snameHash = fptr.add(CBASEFUNC.shortName).readU64();
                    var flags = fptr.add(CBASEFUNC.flags).readU32();
                    var isNative = (flags & 1) !== 0;

                    entry.functions.push({
                        addr: fptr.toString(),
                        fullNameHash: fnameHash.toString(16),
                        shortNameHash: snameHash.toString(16),
                        isNative: isNative,
                        flags: '0x' + flags.toString(16),
                    });
                } catch(e) {}
            }

            // Dump property info
            entry.properties = [];
            for (var k = 0; k < props.length && k < 200; k++) {
                var pptr = props[k];
                if (pptr.isNull()) continue;
                try {
                    var pnameHash = pptr.add(CPROPERTY.name).readU64();
                    var poffset = pptr.add(CPROPERTY.offset).readU32();
                    entry.properties.push({
                        nameHash: pnameHash.toString(16),
                        offset: poffset,
                    });
                } catch(e) {}
            }

        } catch(e) {
            entry.parseError = e.message;
        }

        types['hash_' + nameHash.toString(16)] = entry;
    });

    send({type: 'log', msg: 'Dumped ' + typeCount + ' types'});

    // ---- Walk global functions HashMap ----
    var funcsBase = rttiSystem.add(RTTI.funcs);
    var globalFuncs = {};
    var funcCount = 0;

    walkHashMap(funcsBase, function(nameHash, funcPtr) {
        funcCount++;
        try {
            var fnameHash = funcPtr.add(CBASEFUNC.fullName).readU64();
            var snameHash = funcPtr.add(CBASEFUNC.shortName).readU64();
            var flags = funcPtr.add(CBASEFUNC.flags).readU32();

            globalFuncs['hash_' + nameHash.toString(16)] = {
                addr: funcPtr.toString(),
                fullNameHash: fnameHash.toString(16),
                shortNameHash: snameHash.toString(16),
                isNative: (flags & 1) !== 0,
                flags: '0x' + flags.toString(16),
            };
        } catch(e) {}
    });

    send({type: 'log', msg: 'Dumped ' + funcCount + ' global functions'});

    // ---- Write output ----
    var output = {
        version: '1.0',
        generated: new Date().toISOString(),
        rttiSystemAddr: rttiSystem.toString(),
        stats: {
            types: typeCount,
            globalFunctions: funcCount,
        },
        types: types,
        globalFunctions: globalFuncs,
    };

    var jsonStr = JSON.stringify(output, null, 2);

    // Write to file
    var file = new File('/tmp/rtti_dump.json', 'w');
    file.write(jsonStr);
    file.flush();
    file.close();

    send({type: 'log', msg: 'RTTI dump written to /tmp/rtti_dump.json (' + (jsonStr.length / 1024).toFixed(0) + ' KB)'});
    send({type: 'done', stats: output.stats});
}

// Hook LoadScripts as a trigger point (RTTI is initialized before scripts load)
try {
    var loadScriptsAddr = IMAGE_BASE.add(LOAD_SCRIPTS_OFFSET);
    Interceptor.attach(loadScriptsAddr, {
        onEnter: function(args) {
            send({type: 'log', msg: 'LoadScripts called, triggering RTTI dump...'});
            dumpRTTI();
        }
    });
    send({type: 'log', msg: 'RTTI dumper hook installed at LoadScripts (0x' + LOAD_SCRIPTS_OFFSET.toString(16) + ')'});
} catch(e) {
    send({type: 'error', msg: 'Failed to hook LoadScripts: ' + e.message});
    // Try immediate dump (game may already be past LoadScripts)
    setTimeout(function() {
        send({type: 'log', msg: 'Attempting immediate RTTI dump...'});
        dumpRTTI();
    }, 5000);
}
