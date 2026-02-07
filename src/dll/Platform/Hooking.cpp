#include "stdafx.hpp"
#include "Hooking.hpp"
#include "Platform.hpp"
#include "Platform/CrashHandler.hpp"
#include "Platform/RuntimeValidation.hpp"
#include "Platform/StructuredLogging.hpp"

#ifdef RED4EXT_PLATFORM_MACOS
#include <libkern/OSCacheControl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <array>
#include <atomic>
#include <algorithm>
#include <cstring>
#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <vector>

#ifdef RED4EXT_USE_FRIDA_GUM
#include <frida-gum.h>
#endif

// Hooking backends on macOS:
//  - Frida Gadget: registration-only; actual interception is done externally.
//  - Native inline: patch target __TEXT and create an in-process trampoline.

namespace
{

enum class HookBackend : int32_t
{
    FridaGadget = 0,
    NativeInline = 1,
    FridaGum = 2,
};

std::atomic<int32_t> g_backend{
#ifdef RED4EXT_USE_FRIDA_GADGET
    static_cast<int32_t>(HookBackend::FridaGadget)
#else
    static_cast<int32_t>(HookBackend::NativeInline)
#endif
};

bool g_inTransaction = false;
int g_hookCount = 0;

#ifdef RED4EXT_USE_FRIDA_GUM
std::once_flag g_gumInitOnce;
GumInterceptor* g_gumInterceptor = nullptr;
bool g_gumInTransaction = false;

struct GumHookRecord
{
    void* target = nullptr;
    void* detour = nullptr;
};

std::unordered_map<void*, GumHookRecord> g_gumHooksByOriginal;

bool EnsureGum()
{
    std::call_once(g_gumInitOnce, []() {
        gum_init_embedded();
        g_gumInterceptor = gum_interceptor_obtain();
    });

    return g_gumInterceptor != nullptr;
}
#endif

constexpr uint32_t kPatchSize = 16;
constexpr uint32_t kLdrX16Lit8 = 0x58000050; // LDR X16, #8
constexpr uint32_t kBrX16 = 0xD61F0200;      // BR X16
constexpr uint32_t kBlrX16 = 0xD63F0200;     // BLR X16

inline size_t AlignToPage(size_t aSize)
{
    const size_t pageSize = static_cast<size_t>(sysconf(_SC_PAGESIZE));
    return (aSize + pageSize - 1) & ~(pageSize - 1);
}

inline uint32_t EncodeMovZ(uint32_t rd, uint16_t imm16, uint32_t shift)
{
    // MOVZ Xd, #imm16, LSL #shift
    // Base: 0xD2800000
    const uint32_t hw = (shift / 16) & 0x3;
    return 0xD2800000u | (hw << 21) | (static_cast<uint32_t>(imm16) << 5) | (rd & 0x1F);
}

inline uint32_t EncodeMovK(uint32_t rd, uint16_t imm16, uint32_t shift)
{
    // MOVK Xd, #imm16, LSL #shift
    // Base: 0xF2800000
    const uint32_t hw = (shift / 16) & 0x3;
    return 0xF2800000u | (hw << 21) | (static_cast<uint32_t>(imm16) << 5) | (rd & 0x1F);
}

inline void EmitLoadImm64(uint32_t rd, uint64_t imm, std::vector<uint32_t>& out)
{
    out.push_back(EncodeMovZ(rd, static_cast<uint16_t>(imm & 0xFFFFu), 0));
    out.push_back(EncodeMovK(rd, static_cast<uint16_t>((imm >> 16) & 0xFFFFu), 16));
    out.push_back(EncodeMovK(rd, static_cast<uint16_t>((imm >> 32) & 0xFFFFu), 32));
    out.push_back(EncodeMovK(rd, static_cast<uint16_t>((imm >> 48) & 0xFFFFu), 48));
}

inline uint32_t EncodeLdrX(uint32_t rt, uint32_t rn)
{
    // LDR Xt, [Xn, #0]
    return 0xF9400000u | ((rn & 0x1F) << 5) | (rt & 0x1F);
}

inline uint32_t EncodeLdrW(uint32_t rt, uint32_t rn)
{
    // LDR Wt, [Xn, #0]
    return 0xB9400000u | ((rn & 0x1F) << 5) | (rt & 0x1F);
}

inline void EmitAbsJump(uint64_t target, std::vector<uint32_t>& out)
{
    out.push_back(kLdrX16Lit8);
    out.push_back(kBrX16);
    const uint64_t addr = target;
    out.push_back(static_cast<uint32_t>(addr & 0xFFFFFFFFu));
    out.push_back(static_cast<uint32_t>((addr >> 32) & 0xFFFFFFFFu));
}

inline void EmitAbsCall(uint64_t target, std::vector<uint32_t>& out)
{
    out.push_back(kLdrX16Lit8);
    out.push_back(kBlrX16);
    const uint64_t addr = target;
    out.push_back(static_cast<uint32_t>(addr & 0xFFFFFFFFu));
    out.push_back(static_cast<uint32_t>((addr >> 32) & 0xFFFFFFFFu));
}

inline bool RelocateInstruction(uint32_t instr, uint64_t srcPC, uint64_t /*dstPC*/, std::vector<uint32_t>& out)
{
    // ADR
    if ((instr & 0x9F000000u) == 0x10000000u)
    {
        const uint32_t rd = instr & 0x1Fu;
        uint32_t immlo = (instr >> 29) & 0x3u;
        uint32_t immhi = (instr >> 5) & 0x7FFFFu;
        int32_t imm21 = static_cast<int32_t>((immhi << 2) | immlo);
        if (imm21 & (1 << 20))
        {
            imm21 |= ~((1 << 21) - 1);
        }
        const uint64_t target = srcPC + static_cast<int64_t>(imm21);
        EmitLoadImm64(rd, target, out);
        return true;
    }

    // ADRP
    if ((instr & 0x9F000000u) == 0x90000000u)
    {
        const uint32_t rd = instr & 0x1Fu;
        uint32_t immlo = (instr >> 29) & 0x3u;
        uint32_t immhi = (instr >> 5) & 0x7FFFFu;
        int32_t imm21 = static_cast<int32_t>((immhi << 2) | immlo);
        if (imm21 & (1 << 20))
        {
            imm21 |= ~((1 << 21) - 1);
        }
        const uint64_t srcPage = srcPC & ~0xFFFULL;
        const uint64_t targetPage = srcPage + (static_cast<int64_t>(imm21) << 12);
        EmitLoadImm64(rd, targetPage, out);
        return true;
    }

    // LDR literal (32-bit or 64-bit)
    const uint32_t topByte = instr & 0xFF000000u;
    if (topByte == 0x58000000u || topByte == 0x18000000u)
    {
        const bool is64 = topByte == 0x58000000u;
        const uint32_t rt = instr & 0x1Fu;
        int32_t imm19 = static_cast<int32_t>((instr >> 5) & 0x7FFFFu);
        if (imm19 & (1 << 18))
        {
            imm19 |= ~((1 << 19) - 1);
        }
        const uint64_t literalAddr = srcPC + (static_cast<int64_t>(imm19) << 2);
        EmitLoadImm64(16, literalAddr, out); // X16 scratch
        out.push_back(is64 ? EncodeLdrX(rt, 16) : EncodeLdrW(rt, 16));
        return true;
    }

    // B (unconditional)
    if ((instr & 0xFC000000u) == 0x14000000u)
    {
        int32_t imm26 = static_cast<int32_t>(instr & 0x03FFFFFFu);
        if (imm26 & (1 << 25))
        {
            imm26 |= ~((1 << 26) - 1);
        }
        const uint64_t target = srcPC + (static_cast<int64_t>(imm26) << 2);
        EmitAbsJump(target, out);
        return true;
    }

    // BL
    if ((instr & 0xFC000000u) == 0x94000000u)
    {
        int32_t imm26 = static_cast<int32_t>(instr & 0x03FFFFFFu);
        if (imm26 & (1 << 25))
        {
            imm26 |= ~((1 << 26) - 1);
        }
        const uint64_t target = srcPC + (static_cast<int64_t>(imm26) << 2);
        EmitAbsCall(target, out);
        return true;
    }

    // Default: copy unchanged.
    out.push_back(instr);
    return true;
}

struct HookRecord
{
    void* target = nullptr;
    void* detour = nullptr;
    void* trampoline = nullptr;
    size_t trampolineSize = 0;
    std::array<uint8_t, kPatchSize> original{};
};

std::unordered_map<void*, HookRecord> g_hooksByTrampoline;

bool CreateTrampoline(void* target, void* detour, HookRecord& outRec)
{
    outRec.target = target;
    outRec.detour = detour;

    // Snapshot original bytes
    std::memcpy(outRec.original.data(), target, kPatchSize);

    std::vector<uint32_t> code;
    code.reserve(64);

    const uint64_t srcBase = reinterpret_cast<uint64_t>(target);
    for (uint32_t off = 0; off < kPatchSize; off += 4)
    {
        uint32_t instr = 0;
        std::memcpy(&instr, reinterpret_cast<void*>(srcBase + off), sizeof(instr));
        if (!RelocateInstruction(instr, srcBase + off, 0, code))
        {
            return false;
        }
    }

    // Jump back to the original function after the overwritten bytes.
    EmitAbsJump(srcBase + kPatchSize, code);

    const size_t trampolineBytes = code.size() * sizeof(uint32_t);
    const size_t allocSize = AlignToPage(std::max<size_t>(trampolineBytes, 256));

    void* mem = mmap(nullptr, allocSize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (mem == MAP_FAILED)
    {
        spdlog::error("[Hooking] mmap(RW) failed: errno={}", errno);
        return false;
    }

    std::memcpy(mem, code.data(), trampolineBytes);
    sys_icache_invalidate(mem, trampolineBytes);

    if (mprotect(mem, allocSize, PROT_READ | PROT_EXEC) != 0)
    {
        spdlog::error("[Hooking] mprotect(RX) failed: errno={}", errno);
        munmap(mem, allocSize);
        return false;
    }

    outRec.trampoline = mem;
    outRec.trampolineSize = allocSize;
    return true;
}

int32_t NativeDetourAttach(void** ppPointer, void* pDetour)
{
    void* target = *ppPointer;
    
#ifdef RED4EXT_PLATFORM_MACOS
    Platform::StructuredLogging::PushContext("NativeDetourAttach", "Hooking", "", target);
#endif
    
    if (!target || !pDetour)
    {
        spdlog::error("[Hooking] NativeDetourAttach: null pointer (target={}, detour={})", 
                      fmt::ptr(target), fmt::ptr(pDetour));
        Platform::RuntimeValidation::RecordHookAttempt(target, false, "null pointer");
#ifdef RED4EXT_PLATFORM_MACOS
        Platform::StructuredLogging::LogStructured(spdlog::level::err, "[Hooking]", 
            fmt::format("NativeDetourAttach failed: null pointer (target={}, detour={})", 
                       fmt::ptr(target), fmt::ptr(pDetour)));
        Platform::StructuredLogging::PopContext();
#endif
        return -1;
    }

    // Comprehensive hook target validation
#ifdef RED4EXT_PLATFORM_MACOS
    auto validation = Platform::RuntimeValidation::ValidateHookTarget(target, pDetour);
    if (!validation.isValid)
    {
        spdlog::error("[Hooking] NativeDetourAttach: VALIDATION FAILED for target {}: {}", 
                      fmt::ptr(target), validation.reason);
        if (validation.nearestValidRegion)
        {
            spdlog::error("[Hooking] Nearest valid region: {} size={}", 
                          fmt::ptr(validation.nearestValidRegion), validation.regionSize);
        }
        Platform::RuntimeValidation::RecordHookAttempt(target, false, validation.reason);
        return -1;
    }

    // Additional prologue validation
    std::string prologueReason;
    if (!Platform::RuntimeValidation::ValidateFunctionPrologue(target, prologueReason))
    {
        spdlog::warn("[Hooking] NativeDetourAttach: Prologue validation warning for {}: {}", 
                     fmt::ptr(target), prologueReason);
        // Don't fail - some functions might have unusual prologues
    }
#endif

    HookRecord rec;
    if (!CreateTrampoline(target, pDetour, rec))
    {
        spdlog::error("[Hooking] Failed to create trampoline for {} -> {}", 
                      fmt::ptr(target), fmt::ptr(pDetour));
        Platform::RuntimeValidation::RecordHookAttempt(target, false, "trampoline creation failed");
        return -1;
    }

    // Patch target to jump to detour.
    uint32_t patchWords[4];
    patchWords[0] = kLdrX16Lit8;
    patchWords[1] = kBrX16;
    const uint64_t det = reinterpret_cast<uint64_t>(pDetour);
    patchWords[2] = static_cast<uint32_t>(det & 0xFFFFFFFFu);
    patchWords[3] = static_cast<uint32_t>((det >> 32) & 0xFFFFFFFFu);

    uint32_t oldProt = 0;
    if (!Platform::ProtectMemory(target, kPatchSize, Platform::Memory_ExecuteReadWrite, &oldProt))
    {
        spdlog::error("[Hooking] ProtectMemory(RWX) failed at {} (errno={})", target, errno);
        munmap(rec.trampoline, rec.trampolineSize);
        return -1;
    }

    std::memcpy(target, patchWords, kPatchSize);
    sys_icache_invalidate(target, kPatchSize);
    Platform::ProtectMemory(target, kPatchSize, oldProt, nullptr);

    g_hooksByTrampoline.emplace(rec.trampoline, rec);
    *ppPointer = rec.trampoline;
    
#ifdef RED4EXT_PLATFORM_MACOS
    Platform::RuntimeValidation::RecordHookAttempt(target, true, "hook attached successfully");
    Platform::StructuredLogging::LogStructured(spdlog::level::info, "[Hooking]", 
        fmt::format("Hook attached: target={} detour={} trampoline={}", 
                   fmt::ptr(target), fmt::ptr(pDetour), fmt::ptr(rec.trampoline)));
    Platform::StructuredLogging::PopContext();
#endif
    
    return NO_ERROR;
}

int32_t NativeDetourDetach(void** ppPointer, void* pDetour)
{
    void* trampoline = *ppPointer;
    if (!trampoline || !pDetour)
    {
        return -1;
    }

    auto it = g_hooksByTrampoline.find(trampoline);
    if (it == g_hooksByTrampoline.end())
    {
        return -1;
    }

    HookRecord rec = it->second;
    if (rec.detour != pDetour)
    {
        return -1;
    }

    uint32_t oldProt = 0;
    if (!Platform::ProtectMemory(rec.target, kPatchSize, Platform::Memory_ExecuteReadWrite, &oldProt))
    {
        spdlog::error("[Hooking] ProtectMemory(RWX) failed at {} (errno={})", rec.target, errno);
        return -1;
    }

    std::memcpy(rec.target, rec.original.data(), kPatchSize);
    sys_icache_invalidate(rec.target, kPatchSize);
    Platform::ProtectMemory(rec.target, kPatchSize, oldProt, nullptr);

    munmap(rec.trampoline, rec.trampolineSize);
    g_hooksByTrampoline.erase(it);

    *ppPointer = rec.target;
    return NO_ERROR;
}
}

extern "C" {

void DetourSetBackend(int32_t aBackend)
{
    if (g_inTransaction)
    {
        return;
    }

    if (aBackend != static_cast<int32_t>(HookBackend::FridaGadget) &&
        aBackend != static_cast<int32_t>(HookBackend::NativeInline)
#ifdef RED4EXT_USE_FRIDA_GUM
        && aBackend != static_cast<int32_t>(HookBackend::FridaGum)
#endif
    )
    {
        return;
    }

    g_backend.store(aBackend);
}

int32_t DetourGetBackend()
{
    return g_backend.load();
}

int32_t DetourTransactionBegin()
{
    if (g_inTransaction)
    {
        return -1;
    }

    const auto backend = static_cast<HookBackend>(g_backend.load());
#ifdef RED4EXT_USE_FRIDA_GUM
    if (backend == HookBackend::FridaGum)
    {
        if (!EnsureGum())
        {
            return -1;
        }

        gum_interceptor_begin_transaction(g_gumInterceptor);
        g_gumInTransaction = true;
    }
#endif

    g_inTransaction = true;
    return NO_ERROR;
}

int32_t DetourTransactionCommit()
{
    if (!g_inTransaction)
    {
        return -1;
    }

    const auto backend = static_cast<HookBackend>(g_backend.load());

#ifdef RED4EXT_USE_FRIDA_GUM
    if (backend == HookBackend::FridaGum && g_gumInTransaction)
    {
        gum_interceptor_end_transaction(g_gumInterceptor);
        g_gumInTransaction = false;
    }
#endif

    g_inTransaction = false;

    if (backend == HookBackend::FridaGadget)
    {
        spdlog::info("[Hooking] Transaction commit: {} hooks registered (Frida Gadget backend)", g_hookCount);
    }
    else if (backend == HookBackend::FridaGum)
    {
        spdlog::info("[Hooking] Transaction commit: {} hooks registered (Frida Gum backend)", g_hookCount);
    }

    return NO_ERROR;
}

int32_t DetourTransactionAbort()
{
    if (!g_inTransaction)
    {
        return -1;
    }

#ifdef RED4EXT_USE_FRIDA_GUM
    const auto backend = static_cast<HookBackend>(g_backend.load());
    if (backend == HookBackend::FridaGum && g_gumInTransaction)
    {
        gum_interceptor_end_transaction(g_gumInterceptor);
        g_gumInTransaction = false;
    }
#endif

    g_inTransaction = false;
    g_hookCount = 0;
    return NO_ERROR;
}

int32_t DetourUpdateThread([[maybe_unused]] void* hThread)
{
    return NO_ERROR;
}

int32_t DetourAttach(void** ppPointer, void* pDetour)
{
    if (!g_inTransaction)
    {
        spdlog::error("[Hooking] DetourAttach failed: not in transaction");
        return -1;
    }

    const auto backend = static_cast<HookBackend>(g_backend.load());

    if (backend == HookBackend::FridaGadget)
    {
        void* pTarget = *ppPointer;
        if (!pTarget || !pDetour)
        {
            spdlog::error("[Hooking] DetourAttach failed: null pointer (target={}, detour={})", 
                          fmt::ptr(pTarget), fmt::ptr(pDetour));
#ifdef RED4EXT_PLATFORM_MACOS
            Platform::RuntimeValidation::RecordHookAttempt(pTarget, false, "null pointer");
#endif
            return -1;
        }

#ifdef RED4EXT_PLATFORM_MACOS
        // Comprehensive validation for Frida Gadget hooks
        auto validation = Platform::RuntimeValidation::ValidateHookTarget(pTarget, pDetour);
        if (!validation.isValid)
        {
            spdlog::warn("[Hooking] Hook #{} VALIDATION WARNING for target {}: {}", 
                         g_hookCount + 1, fmt::ptr(pTarget), validation.reason);
            if (validation.nearestValidRegion)
            {
                spdlog::warn("[Hooking] Nearest valid region: {} size={}", 
                             fmt::ptr(validation.nearestValidRegion), validation.regionSize);
            }
            // Don't fail - Frida Gadget might handle invalid addresses differently
            Platform::RuntimeValidation::RecordHookAttempt(pTarget, false, validation.reason);
        }
        else
        {
            Platform::RuntimeValidation::RecordHookAttempt(pTarget, true, "hook registered");
        }
#endif

        g_hookCount++;
        spdlog::info("[Hooking] Hook #{} registered at {} -> {} (Frida Gadget backend)", 
                     g_hookCount, fmt::ptr(pTarget), fmt::ptr(pDetour));
        return NO_ERROR;
    }

    if (backend == HookBackend::FridaGum)
    {
#ifdef RED4EXT_USE_FRIDA_GUM
        if (!EnsureGum())
        {
            spdlog::error("[Hooking] Frida Gum init failed");
            return -1;
        }

        if (!g_gumInTransaction)
        {
            spdlog::error("[Hooking] Frida Gum DetourAttach called outside transaction");
            return -1;
        }

        void* pTarget = *ppPointer;
        gpointer original = nullptr;
        const auto ret = gum_interceptor_replace(g_gumInterceptor, pTarget, pDetour, nullptr, &original);
        if (ret != GUM_REPLACE_OK || original == nullptr)
        {
            spdlog::error("[Hooking] gum_interceptor_replace failed at {} -> {} (ret={})", pTarget, pDetour,
                          static_cast<int>(ret));
            return -1;
        }

        g_gumHooksByOriginal[original] = GumHookRecord{pTarget, pDetour};

        g_hookCount++;
        spdlog::info("[Hooking] Hook #{} installed at {} -> {} (Frida Gum backend)", g_hookCount, pTarget, pDetour);

        *ppPointer = original;
        return NO_ERROR;
#else
        spdlog::error("[Hooking] Frida Gum backend requested but RED4EXT_USE_FRIDA_GUM is not enabled");
        return -1;
#endif
    }

    return NativeDetourAttach(ppPointer, pDetour);
}

int32_t DetourDetach(void** ppPointer, void* pDetour)
{
    if (!g_inTransaction)
    {
        return -1;
    }

    const auto backend = static_cast<HookBackend>(g_backend.load());

    if (backend == HookBackend::FridaGadget)
    {
        return NO_ERROR;
    }

    if (backend == HookBackend::FridaGum)
    {
#ifdef RED4EXT_USE_FRIDA_GUM
        if (!EnsureGum())
        {
            return -1;
        }

        if (!g_gumInTransaction)
        {
            return -1;
        }

        void* original = *ppPointer;
        auto it = g_gumHooksByOriginal.find(original);
        if (it == g_gumHooksByOriginal.end())
        {
            return -1;
        }

        const auto rec = it->second;
        if (rec.detour != pDetour)
        {
            return -1;
        }

        gum_interceptor_revert(g_gumInterceptor, rec.target);
        g_gumHooksByOriginal.erase(it);
        *ppPointer = rec.target;
        return NO_ERROR;
#else
        return -1;
#endif
    }

    return NativeDetourDetach(ppPointer, pDetour);
}

}

#endif
