#pragma once

#ifdef RED4EXT_PLATFORM_MACOS

#include <cstddef>
#include <cstdint>

// Native arm64 hook engine. A 4-byte B in the target reaches a near island.
// The island holds the absolute jump. Trampolines and islands are anonymous
// pages allocated within ±128 MiB. No MAP_JIT.

namespace NativeHook
{

inline constexpr const char* kRefuseCodeWritesFormat =
    "Refusing native code writes: address DB mismatch (db_version='%s' image_version='%s' db_uuid='%s' image_uuid='%s')";

struct ProtectOp
{
    uint32_t prot;
    bool copy;
    bool restore;
};

struct RelocOut
{
    enum class Kind : uint8_t
    {
        Copied,
        Relocated,
        Refused
    };

    Kind kind = Kind::Refused;
    uint32_t words[24]{};
    uint32_t count = 0;
};

struct HookInfo
{
    char name[160];
    char owner[128];
    uint64_t target;
    uint64_t imageOffset;
    int installKr;
    bool installed;
    uint64_t hits;
};

void Begin();
int32_t Attach(void** ppPointer, void* detour);
int32_t Detach(void** ppPointer, void* detour);
int32_t Commit();
int32_t Abort();

void SetIdentity(void* target, const char* name, const char* owner);
const char* CoreHookName(uint32_t hash);

// strict && (version mismatch || uuid mismatch || missing uuid) refuses every
// later code write and fills logLine with kRefuseCodeWritesFormat.
bool AllowWrites(const char* dbVersion, const char* dbUuid, const char* imageVersion, const char* imageUuid,
                 bool strict, char* logLine, size_t logCap);
bool WritesAllowed();

RelocOut Relocate(uint32_t instr, uint64_t srcPc, uint64_t dstPc);

uint32_t CopyProtectLog(ProtectOp* out, uint32_t cap);
uint32_t CopyHookStats(HookInfo* out, uint32_t cap);
bool WriteHookStats(const char* path, int pid, const char* uuid);

void StartStatsThread(const char* path, const char* uuid);
void StopStatsThread();

void MainImageUuid(char* out, size_t cap);
uint64_t MainImageBase();

} // namespace NativeHook

#endif
