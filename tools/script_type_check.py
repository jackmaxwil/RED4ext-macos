#!/usr/bin/env python3
"""Offline check of redscript native imports against the game's RTTI dump.

    script_type_check.py <rtti_layout_macos.json> <scripts dir>... [--plugin-src <dir>]... [--aliases <aliases.json>]
                         [--no-plugin-aliases] [-v]

The game validates every `native` class/struct and every `native let` a script bundle declares against its RTTI at
load, and refuses the whole bundle on the first mismatch ("Failed to initialize scripts data!"). This runs the same
comparison before launch, from data:

  - missing: a native class/struct/enum the scripts declare that the binary does not have;
  - mismatch: a native property whose script type does not name the native type, after applying the type aliases the
    loaded plugins accept (Codeware: ResourceRef -> rRef:*, ResourceAsyncRef -> raRef:*, CurveData* -> curveData:*)
    and the game's own script names for native types.

Exit 0 when clean, 1 when anything would be rejected, 2 on bad input.
"""
import json
import re
import sys
from pathlib import Path

# Plugin-level aliases (script type name -> native type-name prefix): Codeware's ScriptingService::OnValidateTypeName.
# Windows applies them inside the game's name check; macOS applies the same table to the validator's report.
PLUGIN_PREFIX_ALIASES = {
    "ResourceRef": "rRef:",
    "ResourceAsyncRef": "raRef:",
    "ResRef": "raRef:",  # redResourceReferenceScriptToken's script name
    "CurveDataFloat": "curveData:",
    "CurveDataVector2": "curveData:",
    "CurveDataVector3": "curveData:",
    "CurveDataVector4": "curveData:",
    "CurveDataHDRColor": "curveData:",
}

# The game's own script names for native types (script name -> native name). The validator accepts these itself.
GAME_SCRIPT_NAMES = {
    "Bool": "Bool", "Int8": "Int8", "Int16": "Int16", "Int32": "Int32", "Int64": "Int64",
    "Uint8": "Uint8", "Uint16": "Uint16", "Uint32": "Uint32", "Uint64": "Uint64",
    "Float": "Float", "Double": "Double", "String": "String", "CName": "CName", "TweakDBID": "TweakDBID",
    "ResRef": "redResourceReferenceScriptToken", "Variant": "Variant",
    # Script names the SDK headers lack and the namespace rule in type_matches does not cover.
    "MountDescriptor_MountingType": "gameMountDescriptorMountType",
}

CLASS_RE = re.compile(r"\b(?:native|importonly)\s+(?:class|struct)\s+(\w+)(?:\s+extends\s+(\w+))?")
ENUM_RE = re.compile(r"\bnative\s+enum\s+(\w+)")
ADDFIELD_RE = re.compile(r"@addField\((\w+)\)")
TYPE = r"(?:\[(?:[^\[\]]|\[[^\]]*\])*\]|[^;=\[])+?"  # a fixed array `[T; N]` (two levels deep) holds a ';'
PROP_RE = re.compile(r"\bnative\s+(?:persistent\s+)?let\s+(\w+)\s*:\s*(" + TYPE + r")\s*[;=]")


def script_type(t):
    """redscript source type -> the type name redscript writes into the bundle."""
    t = t.strip()
    m = re.fullmatch(r"(\w+)\s*<\s*(.+)\s*>", t)
    if m:
        kind, inner = m.group(1), script_type(m.group(2))
        return {"array": "array:", "ref": "handle:", "wref": "whandle:", "script_ref": "script_ref:"}.get(
            kind, kind + ":") + inner
    m = re.fullmatch(r"\[\s*(.+)\s*;\s*(\d+)\s*\]", t)
    if m:
        return f"static:{m.group(2)},{script_type(m.group(1))}"
    return t


def parse_scripts(dirs):
    classes, enums, props = {}, set(), []
    for d in dirs:
        for f in sorted(Path(d).rglob("*.reds")):
            text = re.sub(r"//[^\n]*", "", f.read_text(errors="replace"))
            text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
            # Walk declarations in order: a native class/struct body owns the `native let`s up to its closing brace;
            # an @addField(X) owns the next declaration.
            depth, owner, owner_depth, pending = 0, None, None, None
            for tok in re.finditer(r"@addField\(\w+\)|\b(?:native|importonly)\s+(?:class|struct)\s+\w+(?:\s+extends\s+\w+)?|"
                                   r"\bnative\s+enum\s+\w+|\bnative\s+(?:persistent\s+)?let\s+\w+\s*:\s*" + TYPE + r"\s*[;=]|"
                                   r"\blet\s+\w+\s*:|[{}]", text):
                s = tok.group(0)
                if s == "{":
                    depth += 1
                elif s == "}":
                    depth -= 1
                    if owner is not None and depth < owner_depth:
                        owner = None
                elif s.startswith("@addField"):
                    pending = ADDFIELD_RE.match(s).group(1)
                elif ENUM_RE.match(s):
                    enums.add(ENUM_RE.match(s).group(1))
                elif CLASS_RE.match(s):
                    m = CLASS_RE.match(s)
                    classes[m.group(1)] = (m.group(2), str(f))
                    owner, owner_depth = m.group(1), depth + 1
                elif PROP_RE.match(s):
                    m = PROP_RE.match(s)
                    cls = pending or (owner if depth == owner_depth else None)
                    if cls:
                        props.append((cls, m.group(1), script_type(m.group(2)), str(f)))
                    pending = None
                else:  # a plain `let` consumes a pending @addField
                    pending = None
    return classes, enums, props


INT_SIZES = {"Int8": 1, "Uint8": 1, "Int16": 2, "Uint16": 2, "Int32": 4, "Uint32": 4, "Int64": 8, "Uint64": 8}
ENUM_KIND, BITFIELD_KIND = 5, 13  # RED::ERTTIType


def native_type(nt):
    """The dump spells fixed arrays both `static:N,T` and `[N]T`; normalize to the first."""
    m = re.fullmatch(r"\[(\d+)\](.+)", nt)
    return f"static:{m.group(1)},{native_type(m.group(2))}" if m else nt


def type_matches(st, nt, rtti_types, aliases):
    nt = native_type(nt)
    if st == nt:
        return True
    for prefix in ("array:", "handle:", "whandle:"):
        if st.startswith(prefix) and nt.startswith(prefix):
            return type_matches(st[len(prefix):], nt[len(prefix):], rtti_types, aliases)
    m, n = re.fullmatch(r"static:(\d+),(.+)", st), re.fullmatch(r"static:(\d+),(.+)", nt)
    if m and n:
        return m.group(1) == n.group(1) and type_matches(m.group(2), n.group(2), rtti_types, aliases)
    if GAME_SCRIPT_NAMES.get(st) == nt or aliases.get(st) == nt:
        return True
    # ponytail: script names the SDK headers lack are taken as "<lowercase namespace><script name>" (gameItemID for
    # ItemID); replace with the binary's script-name table if a wrong match ever slips through.
    if re.fullmatch(r"[a-z]+" + re.escape(st), nt):
        return True
    # Enums and bitfields may be imported as the integer of the same size.
    info = rtti_types.get(nt, {})
    if st in INT_SIZES and info.get("kind") in (ENUM_KIND, BITFIELD_KIND) and info.get("size") == INT_SIZES[st]:
        return True
    target = PLUGIN_PREFIX_ALIASES.get(st)
    return bool(target and nt.startswith(target))


def main(argv):
    verbose = "-v" in argv
    argv = [a for a in argv if a != "-v"]
    aliases = {}
    if "--aliases" in argv:
        i = argv.index("--aliases")
        aliases = json.loads(Path(argv[i + 1]).read_text())
        del argv[i:i + 2]
    plugin_words, plugin_prefixes = set(), set()
    while "--plugin-src" in argv:
        i = argv.index("--plugin-src")
        for f in Path(argv[i + 1]).rglob("*.[ch]pp"):
            text = f.read_text(errors="replace")
            plugin_words |= set(re.findall(r"\w+", text))
            plugin_prefixes |= set(re.findall(r'"(\w+)"\s*#\w+', text))  # "CurveData" #_name in a macro
        del argv[i:i + 2]
    use_plugin_aliases = "--no-plugin-aliases" not in argv
    argv = [a for a in argv if a != "--no-plugin-aliases"]
    if not use_plugin_aliases:
        PLUGIN_PREFIX_ALIASES.clear()

    def plugin_type(n):
        return n in plugin_words or any(n.startswith(p) and n[len(p):] in plugin_words for p in plugin_prefixes)
    if len(argv) < 2:
        print(__doc__, file=sys.stderr)
        return 2
    dump = json.loads(Path(argv[0]).read_text())
    # Script names of native types (script alias -> native name), from the SDK's generated headers next to the dump.
    gen = Path(argv[0]).resolve().parent.parent / "include/RED4ext/Scripting/Natives/Generated"
    for h in gen.rglob("*.hpp"):
        m = re.search(r'NAME = "(\w+)";\s*static constexpr const char\* ALIAS = "(\w+)"', h.read_text(errors="replace"))
        if m and m.group(1) != m.group(2):
            aliases.setdefault(m.group(2), m.group(1))
    rtti_classes, rtti_types = dump["classes"], dump["types"]
    classes, enums, props = parse_scripts(argv[1:])

    # A type the plugin registers itself (named in its sources) is not looked up in the game's RTTI.
    missing = sorted(n for n in classes
                     if n not in rtti_classes and aliases.get(n, n) not in rtti_classes and not plugin_type(n))
    missing += sorted(n for n in enums
                      if n not in rtti_types and aliases.get(n, n) not in rtti_types and not plugin_type(n))
    mismatches, unknown = [], []
    for cls, name, st, f in props:
        native_cls = rtti_classes.get(aliases.get(cls, cls)) or next(
            (v for k, v in rtti_classes.items() if re.fullmatch(r"[a-z]+" + re.escape(cls), k)), None)
        # Walk the native parent chain: an @addField may name a property declared on a base class.
        prop = None
        while native_cls and prop is None:
            prop = next((p for p in native_cls.get("props", []) if p["name"] == name), None)
            native_cls = rtti_classes.get(native_cls.get("parent") or "")
        if prop is None:
            unknown.append((cls, name, f))
        elif not type_matches(st, prop["type"], rtti_types, aliases):
            mismatches.append((cls, name, st, prop["type"], f))

    for n in missing:
        print(f"missing native type: {n} ({classes[n][1] if n in classes else 'enum'})")
    for cls, name, st, nt, f in mismatches:
        print(f"type mismatch: {cls}.{name} script '{st}' vs native '{nt}' ({f})")
    if verbose:
        for cls, name, f in unknown:
            print(f"no native property: {cls}.{name} ({f})")
    print(f"script_type_check: {len(classes)} native classes, {len(props)} native properties; "
          f"{len(missing)} missing, {len(mismatches)} mismatched, {len(unknown)} not found as native properties")
    return 1 if missing or mismatches else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
