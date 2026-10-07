# Fixes to offer upstream

These changes from the macOS port also fix real bugs on other platforms or compilers. They are written to be portable: Windows behaviour is unchanged.

## RedLib (bundled in TweakXL/ArchiveXL as `lib/Red`)
- **Registrars never run under clang.** `ClassDefinition<T>::s_registrar` and its siblings are inline static members of class templates. Clang does not initialize them unless they are used; MSVC initializes them eagerly. The fix (`lib/Red/TypeInfo/Macros/Definition.hpp`) explicitly instantiates each definition after its `TypeInfoBuilder` specialization. One caveat: `FlagsDefinition` must instantiate its `EnumDefinition<E, true>` base, which is where the registrar lives.
- **Enum names under clang.** `nameof_full_type` gives `RED4ext::ns::Enum` without the elaborated `enum` keyword, so `ScopedEnumPrefix` never matched and every SDK enum without `NAME` resolved to a wrong RTTI name. Fixed in `lib/Red/TypeInfo/Resolving.hpp`.
- **`ClassDescriptorDefaultImpl::IsEqual`** returned `const bool`, which conflicts with the base declaration once the template is instantiated.

## RED4ext.SDK
- **The SDK dumper reads `CClass::unk118`**, a lazily built cache that is empty for classes not yet used. It should read `props` (+0x28) and walk `parent` (`include/RED4ext/Dump/Reflection-inl.hpp`).
- **Platform ABI notes for clang/Itanium targets,** all in `docs/re/*.md`:
  - struct results are returned through x8, not as a hidden first argument;
  - vtables have two destructor slots, so every later slot shifts by 8;
  - non-POD bases have their tail padding reused.
- **`Handle(T*)`** has no out-of-line game function on clang builds. It is inlined; see `include/RED4ext/Handle.hpp`.
- **TweakDB buffer growth** left the new tail of the buffer uninitialized. It is now zeroed.

## TweakXL
- **`MetadataImporter`** read names of up to 255 bytes into a 254-byte buffer.
- **`TweakExecutor::ExecuteTweaks`** treated every class as a tweak when `ScriptableTweak` was not registered, because a null base class matches everything.
- **`CollectRecordInfo`** now checks that the helper functions it skips belong to the property, and fails closed on any other function order.
