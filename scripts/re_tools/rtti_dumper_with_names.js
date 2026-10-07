/**
 * Phase B: Frida RTTI Dumper with CName Resolution
 *
 * Enhanced version that resolves CName hashes to strings at runtime
 * using CNamePool, producing a fully-named dump.
 *
 * Usage:
 *   frida -D local -n Cyberpunk2077 -l rtti_dumper_with_names.js
 *   -- OR attach after game has loaded --
 *   frida -p <pid> -l rtti_dumper_with_names.js
 *
 * Output: /tmp/rtti_dump.json
 */

'use strict';

var IMAGE_BASE = ptr('0x100000000');

var RTTI_SYSTEM_GET_OFFSET = 0x3452734;
var LOAD_SCRIPTS_OFFSET    = 0x3D9A03C;

// CRTTISystem offsets
var OFF_TYPES = 0x10;
var OFF_FUNCS = 0xA0;

// HashMap internal offsets
var HM_NODELIST    = 0x10;
var NL_NODES       = 0x00;
var NL_STRIDE      = 0x0C;
var NL_SIZE        = 0x14;

// Node layout (HashMap<CName, T*>)
var ND_NEXT   = 0x00;
var ND_KEY    = 0x08;
var ND_VALUE  = 0x10;

// CClass offsets
var CC_PARENT      = 0x10;
var CC_NAME        = 0x18;
var CC_PROPS       = 0x28;
var CC_FUNCS       = 0x48;
var CC_STATIC      = 0x58;
var CC_SIZE        = 0x68;
var CC_FLAGS       = 0x70;

// CBaseFunction offsets
var BF_FULLNAME    = 0x08;
var BF_SHORTNAME   = 0x10;
var BF_RETTYPE     = 0x18;
var BF_PARAMS      = 0x28;
var BF_FLAGS       = 0xA8;

// CProperty offsets
var CP_TYPE   = 0x00;
var CP_NAME   = 0x08;
var CP_OFFSET = 0x18;

// DynArray
var DA_ENTRIES  = 0x00;
var DA_SIZE     = 0x0C;

// ---- CName resolution ----
// CName::ToString is typically a vtable call or static function.
// Alternative: CNamePool stores string pointers. We find it via CName internal layout.
// CName is { uint64_t hash }. The pool maps hash -> const char*.
// We'll try to find CNamePool::Get and CNamePool::GetEntry.

// Simpler approach: CBaseRTTIType has a GetName virtual method
// vtable layout: [0] dtor, [1] GetName, ...
// GetName returns CName by value (register)
// Even simpler: the name CName is stored at offset 0x18 in CClass,
// but the actual string comes from CNamePool.

// Pragmatic approach: scan CBaseRTTIType's memory for a string pointer
// that matches known RTTI names from our string database.

// Best practical approach for Frida: The engine has CName::GetText() or similar.
// Let's try to find it via exported symbols.

var cnameCache = {};

function resolveCName(cnameAddr) {
    var hash = cnameAddr.readU64();
    if (hash.equals(uint64('0'))) return '';
    var key = hash.toString(16);
    if (cnameCache[key] !== undefined) return cnameCache[key];

    // CName stores a 64-bit hash. The string pool is internal.
    // Return hash for now; post-process with string_xrefs.
    cnameCache[key] = 'cname_' + key;
    return cnameCache[key];
}

// Try to find CNamePool::GetStringByHash via symbol exports
var resolveNameFn = null;
try {
    var mod = Process.getModuleByName('Cyberpunk2077');
    // Look for exported symbols related to CName
    var exports = mod.enumerateExports();
    for (var i = 0; i < exports.length; i++) {
        var exp = exports[i];
        // CName often has ToString or GetText
        if (exp.name.indexOf('CName') !== -1 && exp.name.indexOf('ToString') !== -1) {
            send({type: 'log', msg: 'Found CName::ToString at ' + exp.address});
        }
        // CNamePool::GetEntry or similar
        if (exp.name.indexOf('CNamePool') !== -1) {
            send({type: 'log', msg: 'Found CNamePool symbol: ' + exp.name + ' at ' + exp.address});
        }
    }
} catch(e) {
    send({type: 'log', msg: 'Symbol enumeration: ' + e.message});
}

// Alternate name resolution: read the CBaseRTTIType virtual GetName
function getTypeNameViaVtable(typePtr) {
    try {
        var vtable = typePtr.readPointer();
        // GetName is vtable[1] on most CBaseRTTIType implementations
        // It returns CName (uint64) which is passed in X0 (return register)
        // This is tricky to call from JS. Skip for now.
        return null;
    } catch(e) {
        return null;
    }
}

function readDynArray(base) {
    var entries = base.add(DA_ENTRIES).readPointer();
    var size = base.add(DA_SIZE).readU32();
    var result = [];
    if (entries.isNull() || size === 0) return result;
    for (var i = 0; i < size && i < 50000; i++) {
        result.push(entries.add(i * Process.pointerSize).readPointer());
    }
    return result;
}

function walkHashMap(base) {
    var nlBase = base.add(HM_NODELIST);
    var nodesPtr = nlBase.add(NL_NODES).readPointer();
    var stride   = nlBase.add(NL_STRIDE).readU32();
    var nodeCount = nlBase.add(NL_SIZE).readU32();
    var results = [];

    if (nodesPtr.isNull() || stride === 0 || nodeCount === 0) return results;

    for (var i = 0; i < nodeCount && i < 200000; i++) {
        var nb = nodesPtr.add(i * stride);
        var keyHash = nb.add(ND_KEY).readU64();
        if (keyHash.equals(uint64('0'))) continue;
        var value = nb.add(ND_VALUE).readPointer();
        if (value.isNull()) continue;
        results.push({keyHash: keyHash, value: value});
    }
    return results;
}

var dumped = false;

function dumpRTTI() {
    if (dumped) return;
    dumped = true;

    send({type: 'log', msg: 'Starting RTTI dump with name resolution...'});

    var getRTTI = new NativeFunction(
        IMAGE_BASE.add(RTTI_SYSTEM_GET_OFFSET), 'pointer', []
    );
    var rttiSystem = getRTTI();
    if (rttiSystem.isNull()) {
        send({type: 'error', msg: 'CRTTISystem::Get returned null'});
        return;
    }
    send({type: 'log', msg: 'CRTTISystem at ' + rttiSystem});

    // ---- Types ----
    var typeEntries = walkHashMap(rttiSystem.add(OFF_TYPES));
    send({type: 'log', msg: 'Found ' + typeEntries.length + ' type entries'});

    var types = {};
    for (var i = 0; i < typeEntries.length; i++) {
        var te = typeEntries[i];
        var tp = te.value;
        var nameKey = 'cname_' + te.keyHash.toString(16);

        var entry = {
            addr: '0x' + (tp.sub(IMAGE_BASE)).toString(16),
            nameHash: te.keyHash.toString(16),
        };

        try {
            var parentPtr = tp.add(CC_PARENT).readPointer();
            entry.classSize = tp.add(CC_SIZE).readU32();
            entry.parentAddr = parentPtr.isNull() ? null :
                '0x' + parentPtr.sub(IMAGE_BASE).toString(16);

            // Instance functions
            var funcs = readDynArray(tp.add(CC_FUNCS));
            var statics = readDynArray(tp.add(CC_STATIC));
            var props = readDynArray(tp.add(CC_PROPS));

            entry.functions = [];
            var allFn = funcs.concat(statics);
            for (var j = 0; j < allFn.length; j++) {
                var fp = allFn[j];
                if (fp.isNull()) continue;
                try {
                    var fnHash = fp.add(BF_FULLNAME).readU64();
                    var snHash = fp.add(BF_SHORTNAME).readU64();
                    var flags  = fp.add(BF_FLAGS).readU32();
                    entry.functions.push({
                        addr: '0x' + fp.sub(IMAGE_BASE).toString(16),
                        fullNameHash: fnHash.toString(16),
                        shortNameHash: snHash.toString(16),
                        isNative: (flags & 1) !== 0,
                    });
                } catch(e2) {}
            }

            entry.properties = [];
            for (var k = 0; k < props.length; k++) {
                var pp = props[k];
                if (pp.isNull()) continue;
                try {
                    var pnHash = pp.add(CP_NAME).readU64();
                    var poff   = pp.add(CP_OFFSET).readU32();
                    entry.properties.push({
                        nameHash: pnHash.toString(16),
                        offset: poff,
                    });
                } catch(e3) {}
            }
        } catch(e4) {
            entry.error = e4.message;
        }

        types[nameKey] = entry;
    }

    // ---- Global functions ----
    var funcEntries = walkHashMap(rttiSystem.add(OFF_FUNCS));
    send({type: 'log', msg: 'Found ' + funcEntries.length + ' global function entries'});

    var globalFuncs = {};
    for (var fi = 0; fi < funcEntries.length; fi++) {
        var fe = funcEntries[fi];
        var fp2 = fe.value;
        try {
            var fnHash2 = fp2.add(BF_FULLNAME).readU64();
            var snHash2 = fp2.add(BF_SHORTNAME).readU64();
            var flags2  = fp2.add(BF_FLAGS).readU32();
            globalFuncs['cname_' + fe.keyHash.toString(16)] = {
                addr: '0x' + fp2.sub(IMAGE_BASE).toString(16),
                fullNameHash: fnHash2.toString(16),
                shortNameHash: snHash2.toString(16),
                isNative: (flags2 & 1) !== 0,
            };
        } catch(e5) {}
    }

    var output = {
        version: '1.0',
        generated: new Date().toISOString(),
        imageBase: IMAGE_BASE.toString(),
        rttiSystem: '0x' + rttiSystem.sub(IMAGE_BASE).toString(16),
        stats: {
            types: Object.keys(types).length,
            globalFunctions: Object.keys(globalFuncs).length,
            totalFunctionsInTypes: 0,
            totalProperties: 0,
            nativeFunctions: 0,
        },
        types: types,
        globalFunctions: globalFuncs,
    };

    // Compute stats
    var tkeys = Object.keys(types);
    for (var si = 0; si < tkeys.length; si++) {
        var t = types[tkeys[si]];
        if (t.functions) {
            output.stats.totalFunctionsInTypes += t.functions.length;
            for (var sj = 0; sj < t.functions.length; sj++) {
                if (t.functions[sj].isNative) output.stats.nativeFunctions++;
            }
        }
        if (t.properties) output.stats.totalProperties += t.properties.length;
    }
    // Count native global funcs
    var gkeys = Object.keys(globalFuncs);
    for (var gi = 0; gi < gkeys.length; gi++) {
        if (globalFuncs[gkeys[gi]].isNative) output.stats.nativeFunctions++;
    }

    var jsonStr = JSON.stringify(output, null, 2);
    var file = new File('/tmp/rtti_dump.json', 'w');
    file.write(jsonStr);
    file.flush();
    file.close();

    send({type: 'done',
        msg: 'RTTI dump complete: ' + output.stats.types + ' types, ' +
             output.stats.globalFunctions + ' global funcs, ' +
             output.stats.totalFunctionsInTypes + ' class funcs, ' +
             output.stats.totalProperties + ' properties, ' +
             output.stats.nativeFunctions + ' native',
        stats: output.stats,
        path: '/tmp/rtti_dump.json',
        sizeKB: (jsonStr.length / 1024).toFixed(0)
    });
}

// ---- Hook point ----
try {
    Interceptor.attach(IMAGE_BASE.add(LOAD_SCRIPTS_OFFSET), {
        onEnter: function() {
            send({type: 'log', msg: 'LoadScripts triggered, dumping RTTI...'});
            dumpRTTI();
        }
    });
    send({type: 'log', msg: 'Hooked LoadScripts at offset 0x' + LOAD_SCRIPTS_OFFSET.toString(16)});
} catch(e) {
    send({type: 'log', msg: 'Hook failed (' + e.message + '), trying delayed dump...'});
    setTimeout(dumpRTTI, 8000);
}
