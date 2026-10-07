#pragma once

#ifdef RED4EXT_PLATFORM_MACOS
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace Platform
{
namespace Diagnostics
{
// Comprehensive system diagnostics
struct SystemDiagnostics
{
    struct AddressResolutionStats
    {
        uint32_t totalResolutions = 0;
        uint32_t successfulResolutions = 0;
        uint32_t failedResolutions = 0;
        uint32_t symbolResolutions = 0;
        uint32_t databaseResolutions = 0;
        std::vector<std::pair<uint32_t, std::string>> failedHashes;
    };

    struct HookStats
    {
        uint32_t totalHooks = 0;
        uint32_t successfulHooks = 0;
        uint32_t failedHooks = 0;
        uint32_t invalidTargets = 0;
        std::chrono::milliseconds totalHookTime{0};
    };

    struct MemoryStats
    {
        size_t totalMappedRegions = 0;
        size_t executableRegions = 0;
        size_t writableRegions = 0;
        void* gameBaseMin = nullptr;
        void* gameBaseMax = nullptr;
    };

    AddressResolutionStats addressResolution;
    HookStats hooks;
    MemoryStats memory;
    std::vector<std::string> warnings;
    std::vector<std::string> errors;
};

// Generate comprehensive diagnostics report
SystemDiagnostics GenerateDiagnostics();

// Export diagnostics to file
bool ExportDiagnostics(const std::string& aFilePath);

// Validate system integrity
struct IntegrityCheck
{
    bool isValid = true;
    std::vector<std::string> issues;
};
IntegrityCheck ValidateSystemIntegrity();

// Memory corruption detection
namespace MemoryCorruption
{
// Canary value for detecting corruption
constexpr uint64_t kCanaryValue = 0xDEADBEEFCAFEBABEULL;

struct Canary
{
    uint64_t value = kCanaryValue;
    uint64_t timestamp = 0;
};

// Place canary before/after critical structures
void* PlaceCanary(void* aAddress, size_t aSize);

// Check canary integrity
bool CheckCanary(void* aCanaryAddress);

// Remove canary
void RemoveCanary(void* aCanaryAddress);
} // namespace MemoryCorruption

// Performance monitoring
namespace Performance
{
struct HookTiming
{
    void* target = nullptr;
    std::chrono::microseconds attachTime{0};
    std::chrono::microseconds detachTime{0};
    uint32_t callCount = 0;
};

// Record hook attach timing
void RecordHookAttach(void* aTarget, std::chrono::microseconds aTime);

// Record hook detach timing
void RecordHookDetach(void* aTarget, std::chrono::microseconds aTime);

// Get hook timing statistics
std::vector<HookTiming> GetHookTimings();

// Get slowest hooks
std::vector<HookTiming> GetSlowestHooks(size_t aCount = 10);
} // namespace Performance
} // namespace Diagnostics
} // namespace Platform
#endif
