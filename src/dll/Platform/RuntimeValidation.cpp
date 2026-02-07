#include "RuntimeValidation.hpp"

#ifdef RED4EXT_PLATFORM_MACOS
#include "CrashHandler.hpp"
#include "Addresses.hpp"
#include "App.hpp"
#include <mach-o/dyld.h>
#include <mach/mach.h>
#include <mach/vm_map.h>
#include <mach/vm_prot.h>
#include <sys/mman.h>
#include <cstring>
#include <atomic>
#include <mutex>
#include <spdlog/spdlog.h>
#include <fmt/format.h>

namespace Platform
{
namespace RuntimeValidation
{
namespace
{
// Expected game executable base (Cyberpunk 2077 uses 0x100000000)
constexpr uintptr_t kExpectedGameBase = 0x100000000ULL;
constexpr size_t kExpectedGameSize = 0x10000000ULL; // ~256MB reasonable range

// ARM64 prologue patterns (common function starts)
constexpr ProloguePattern kValidPrologues[] = {
    // STP X29, X30, [SP, #-0x??]!  (frame pointer save)
    {0xFFC00000, 0xA9800000, "STP X29,X30 frame save"},
    // SUB SP, SP, #imm  (stack allocation)
    {0xFF800000, 0xD1000000, "SUB SP stack allocation"},
    // MOV X29, SP  (frame pointer setup)
    {0xFFE0001F, 0xAA1F03FD, "MOV X29,SP"},
    // STP X19, X20, [SP, #-0x??]!  (callee-saved regs)
    {0xFFC00000, 0xA9000000, "STP callee-saved"},
};

std::atomic<uint32_t> g_totalHookAttempts{0};
std::atomic<uint32_t> g_successfulHooks{0};
std::atomic<uint32_t> g_failedHooks{0};
std::atomic<uint32_t> g_invalidTargets{0};
std::mutex g_hookDetailsMutex;
std::vector<std::pair<void*, std::string>> g_failedHookDetails;

void* g_gameBaseMin = nullptr;
void* g_gameBaseMax = nullptr;
std::once_flag g_rangeInitFlag;

void InitializeGameRange()
{
    std::call_once(g_rangeInitFlag, []() {
        const uint32_t imageCount = _dyld_image_count();
        for (uint32_t i = 0; i < imageCount; ++i)
        {
            const char* imageName = _dyld_get_image_name(i);
            if (!imageName)
            {
                continue;
            }

            std::string name(imageName);
            if (name.find("Cyberpunk2077") != std::string::npos ||
                name.find("Cyberpunk2077.app") != std::string::npos)
            {
                const struct mach_header_64* header =
                    reinterpret_cast<const struct mach_header_64*>(_dyld_get_image_header(i));
                intptr_t slide = _dyld_get_image_vmaddr_slide(i);
                uintptr_t base = reinterpret_cast<uintptr_t>(header) - slide;

                g_gameBaseMin = reinterpret_cast<void*>(base);
                g_gameBaseMax = reinterpret_cast<void*>(base + 0x20000000); // ~512MB max

                spdlog::info("[RuntimeValidation] Game range: {} - {}", 
                             fmt::ptr(g_gameBaseMin), fmt::ptr(g_gameBaseMax));
                break;
            }
        }

        if (!g_gameBaseMin)
        {
            // Fallback to expected range
            g_gameBaseMin = reinterpret_cast<void*>(kExpectedGameBase);
            g_gameBaseMax = reinterpret_cast<void*>(kExpectedGameBase + kExpectedGameSize);
            spdlog::warn("[RuntimeValidation] Using fallback game range: {} - {}", 
                         fmt::ptr(g_gameBaseMin), fmt::ptr(g_gameBaseMax));
        }
    });
}
}

bool ValidateFunctionPrologue(void* aAddress, std::string& outReason)
{
    if (!aAddress)
    {
        outReason = "null pointer";
        return false;
    }

    if (!CrashHandler::IsValidAddress(aAddress, 4))
    {
        outReason = "address not in mapped memory";
        return false;
    }

    uint32_t firstInstr = 0;
    std::memcpy(&firstInstr, aAddress, sizeof(firstInstr));

    // Check against known prologue patterns
    for (const auto& pattern : kValidPrologues)
    {
        if ((firstInstr & pattern.mask) == pattern.value)
        {
            outReason = fmt::format("valid prologue: {} ({:#x})", pattern.name, firstInstr);
            return true;
        }
    }

    // Check for common invalid patterns
    if (firstInstr == 0x00000000 || firstInstr == 0xFFFFFFFF)
    {
        outReason = fmt::format("invalid: all zeros/ones ({:#x})", firstInstr);
        return false;
    }

    // Check if it's a branch instruction (might be a trampoline)
    if ((firstInstr & 0xFC000000) == 0x14000000) // B
    {
        outReason = fmt::format("branch instruction (possible trampoline) ({:#x})", firstInstr);
        return false;
    }

    // If we can't validate, be conservative
    outReason = fmt::format("unknown prologue pattern ({:#x})", firstInstr);
    return false;
}

bool IsAddressInGameRange(void* aAddress)
{
    InitializeGameRange();
    
    if (!g_gameBaseMin || !g_gameBaseMax)
    {
        return false;
    }

    uintptr_t addr = reinterpret_cast<uintptr_t>(aAddress);
    uintptr_t min = reinterpret_cast<uintptr_t>(g_gameBaseMin);
    uintptr_t max = reinterpret_cast<uintptr_t>(g_gameBaseMax);

    return (addr >= min && addr < max);
}

void GetGameAddressRange(void** outMin, void** outMax)
{
    InitializeGameRange();
    if (outMin)
    {
        *outMin = g_gameBaseMin;
    }
    if (outMax)
    {
        *outMax = g_gameBaseMax;
    }
}

bool ValidateExecutableRegion(void* aAddress, size_t aSize)
{
    if (!aAddress)
    {
        return false;
    }

    CrashHandler::MemoryRegionInfo regionInfo;
    if (!CrashHandler::GetMemoryRegionInfo(aAddress, regionInfo))
    {
        return false;
    }

    // Check if region has execute permission
    return (regionInfo.protection & VM_PROT_EXECUTE) != 0;
}

bool IsValidARM64Code(void* aAddress, size_t aSize)
{
    if (!aAddress || aSize == 0)
    {
        return false;
    }

    if (!CrashHandler::IsValidAddress(aAddress, aSize))
    {
        return false;
    }

    // Check first few instructions for valid ARM64 patterns
    const size_t checkSize = std::min(aSize, size_t(64));
    for (size_t i = 0; i < checkSize; i += 4)
    {
        uint32_t instr = 0;
        std::memcpy(&instr, reinterpret_cast<char*>(aAddress) + i, sizeof(instr));

        // Check for invalid patterns
        if (instr == 0x00000000 || instr == 0xFFFFFFFF)
        {
            return false;
        }

        // Check for some valid instruction patterns
        // This is a heuristic - real validation would require full disassembly
        uint32_t opcode = instr & 0xFF000000;
        if (opcode == 0x00000000 && instr != 0x00000000)
        {
            // Suspicious - might be data
            continue;
        }
    }

    return true;
}

bool IsBranchIsland(void* aAddress)
{
    if (!aAddress)
    {
        return false;
    }

    uint32_t firstInstr = 0;
    std::memcpy(&firstInstr, aAddress, sizeof(firstInstr));

    // Branch instructions
    if ((firstInstr & 0xFC000000) == 0x14000000) // B
    {
        return true;
    }
    if ((firstInstr & 0xFC000000) == 0x94000000) // BL
    {
        return true;
    }

    // LDR + BR pattern (common in branch islands)
    if ((firstInstr & 0xFF000000) == 0x58000000) // LDR literal
    {
        uint32_t secondInstr = 0;
        std::memcpy(&secondInstr, reinterpret_cast<char*>(aAddress) + 4, sizeof(secondInstr));
        if ((secondInstr & 0xFFFFFC00) == 0xD61F0000) // BR
        {
            return true;
        }
    }

    return false;
}

HookValidationResult ValidateHookTarget(void* aTarget, void* aDetour)
{
    HookValidationResult result;

    if (!aTarget)
    {
        result.reason = "target is null";
        return result;
    }

    if (!aDetour)
    {
        result.reason = "detour is null";
        return result;
    }

    // Check if target is in valid memory
    if (!CrashHandler::IsValidAddress(aTarget, 16))
    {
        CrashHandler::MemoryRegionInfo regionInfo;
        if (CrashHandler::GetMemoryRegionInfo(aTarget, regionInfo))
        {
            result.nearestValidRegion = regionInfo.start;
            result.regionSize = regionInfo.size;
            result.reason = fmt::format("target {} not in mapped memory (nearest: {} size:{})", 
                                       fmt::ptr(aTarget), fmt::ptr(regionInfo.start), regionInfo.size);
        }
        else
        {
            result.reason = fmt::format("target {} not in any mapped memory region", fmt::ptr(aTarget));
        }
        return result;
    }

    // Check if target is in expected game range
    if (!IsAddressInGameRange(aTarget))
    {
        void* min = nullptr, *max = nullptr;
        GetGameAddressRange(&min, &max);
        result.reason = fmt::format("target {} outside expected game range ({} - {})", 
                                    fmt::ptr(aTarget), fmt::ptr(min), fmt::ptr(max));
        // Don't fail - might be valid library code
    }

    // Validate prologue
    std::string prologueReason;
    if (!ValidateFunctionPrologue(aTarget, prologueReason))
    {
        result.reason = fmt::format("invalid prologue: {}", prologueReason);
        return result;
    }

    // Check if it's a branch island (might be intentional, but warn)
    if (IsBranchIsland(aTarget))
    {
        result.reason = fmt::format("target appears to be a branch island/trampoline");
        // Don't fail - some hooks intentionally target trampolines
    }

    // Validate executable region
    if (!ValidateExecutableRegion(aTarget, 16))
    {
        result.reason = fmt::format("target not in executable memory region");
        return result;
    }

    result.isValid = true;
    result.reason = "validation passed";
    return result;
}

HealthCheckResult PerformHealthCheck()
{
    HealthCheckResult result;

    // Check game address range
    InitializeGameRange();
    if (!g_gameBaseMin || !g_gameBaseMax)
    {
        result.errors.push_back("Could not determine game executable address range");
        result.isHealthy = false;
    }

    // Check hook statistics
    auto stats = GetHookStatistics();
    if (stats.failedHooks > 0)
    {
        result.warnings.push_back(fmt::format("{} hook(s) failed to attach", stats.failedHooks));
    }
    if (stats.invalidTargets > 0)
    {
        result.warnings.push_back(fmt::format("{} hook target(s) were invalid", stats.invalidTargets));
    }

    // Validate critical addresses
    auto* app = App::Get();
    if (app)
    {
        auto* addresses = Addresses::Instance();
        if (addresses)
        {
            // Check a few critical hashes resolve correctly
            constexpr uint32_t criticalHashes[] = {
                240386859UL,        // Main
                4223801011UL,       // CGameApplication_AddState
            };

            for (auto hash : criticalHashes)
            {
                auto addr = addresses->Resolve(hash);
                if (addr == 0)
                {
                    result.errors.push_back(fmt::format("Critical hash 0x{:08X} failed to resolve", hash));
                    result.isHealthy = false;
                }
                else if (!IsAddressInGameRange(reinterpret_cast<void*>(addr)))
                {
                    result.warnings.push_back(fmt::format("Hash 0x{:08X} resolved to address {} outside game range", 
                                                          hash, fmt::ptr(reinterpret_cast<void*>(addr))));
                }
            }
        }
    }

    return result;
}

HookStats GetHookStatistics()
{
    HookStats stats;
    stats.totalHooks = g_totalHookAttempts.load();
    stats.successfulHooks = g_successfulHooks.load();
    stats.failedHooks = g_failedHooks.load();
    stats.invalidTargets = g_invalidTargets.load();

    std::lock_guard<std::mutex> lock(g_hookDetailsMutex);
    stats.failedHookDetails = g_failedHookDetails;

    return stats;
}

void RecordHookAttempt(void* aTarget, bool aSuccess, const std::string& aReason)
{
    g_totalHookAttempts.fetch_add(1);

    if (aSuccess)
    {
        g_successfulHooks.fetch_add(1);
    }
    else
    {
        g_failedHooks.fetch_add(1);
        
        std::lock_guard<std::mutex> lock(g_hookDetailsMutex);
        if (g_failedHookDetails.size() < 50) // Limit to prevent memory bloat
        {
            g_failedHookDetails.emplace_back(aTarget, aReason);
        }
    }

    // Check if target was invalid
    if (aTarget && !IsAddressInGameRange(aTarget))
    {
        g_invalidTargets.fetch_add(1);
    }
}

bool IsAddressInRange(void* aAddress, void* aRangeStart, size_t aRangeSize)
{
    if (!aAddress || !aRangeStart)
    {
        return false;
    }

    uintptr_t addr = reinterpret_cast<uintptr_t>(aAddress);
    uintptr_t start = reinterpret_cast<uintptr_t>(aRangeStart);
    uintptr_t end = start + aRangeSize;

    return (addr >= start && addr < end);
}

void* FindNearestValidRegion(void* aAddress, size_t aMinSize)
{
    if (!aAddress)
    {
        return nullptr;
    }

    CrashHandler::MemoryRegionInfo regionInfo;
    if (CrashHandler::GetMemoryRegionInfo(aAddress, regionInfo))
    {
        if (regionInfo.size >= aMinSize)
        {
            return regionInfo.start;
        }
    }

    return nullptr;
}
}
}
#endif
