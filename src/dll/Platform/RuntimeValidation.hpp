#pragma once

#ifdef RED4EXT_PLATFORM_MACOS
#include <cstdint>
#include <string>
#include <vector>

namespace Platform
{
namespace RuntimeValidation
{
// ARM64 function prologue patterns
struct ProloguePattern
{
    uint32_t mask;    // Bits to match
    uint32_t value;   // Expected value
    const char* name; // Description
};

// Validate that a function address has a valid ARM64 prologue
// Returns true if prologue looks valid, false otherwise
bool ValidateFunctionPrologue(void* aAddress, std::string& outReason);

// Check if address is within expected game executable range
bool IsAddressInGameRange(void* aAddress);

// Get expected game executable address range
void GetGameAddressRange(void** outMin, void** outMax);

// Validate memory region is executable and readable
bool ValidateExecutableRegion(void* aAddress, size_t aSize);

// Check for common ARM64 instruction patterns that indicate valid code
bool IsValidARM64Code(void* aAddress, size_t aSize = 64);

// Detect if address points to a branch island or trampoline
bool IsBranchIsland(void* aAddress);

// Validate hook target before attachment
struct HookValidationResult
{
    bool isValid = false;
    std::string reason;
    void* nearestValidRegion = nullptr;
    size_t regionSize = 0;
};
HookValidationResult ValidateHookTarget(void* aTarget, void* aDetour);

// Runtime health check - validate critical system state
struct HealthCheckResult
{
    bool isHealthy = true;
    std::vector<std::string> warnings;
    std::vector<std::string> errors;
};
HealthCheckResult PerformHealthCheck();

// Track hook statistics
struct HookStats
{
    uint32_t totalHooks = 0;
    uint32_t successfulHooks = 0;
    uint32_t failedHooks = 0;
    uint32_t invalidTargets = 0;
    std::vector<std::pair<void*, std::string>> failedHookDetails;
};
HookStats GetHookStatistics();
void RecordHookAttempt(void* aTarget, bool aSuccess, const std::string& aReason = "");

// Address range validation helpers
bool IsAddressInRange(void* aAddress, void* aRangeStart, size_t aRangeSize);
void* FindNearestValidRegion(void* aAddress, size_t aMinSize = 16);
} // namespace RuntimeValidation
} // namespace Platform
#endif
