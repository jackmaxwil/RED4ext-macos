#pragma once

#ifdef RED4EXT_PLATFORM_MACOS
#include <cstdint>
#include <string>

namespace Platform
{
namespace CrashHandler
{
    // Initialize crash handlers (signal handlers, etc.)
    void Initialize();

    // Capture current stack trace (up to maxDepth frames)
    std::string CaptureStackTrace(uint32_t maxDepth = 32);

    // Validate that an address is within a valid memory region
    bool IsValidAddress(void* aAddress, size_t aSize = 1);

    // Get memory region info for an address
    struct MemoryRegionInfo
    {
        void* start = nullptr;
        size_t size = 0;
        uint32_t protection = 0;
        std::string name;
    };
    bool GetMemoryRegionInfo(void* aAddress, MemoryRegionInfo& outInfo);

    // Log all loaded dyld images (useful for debugging)
    void LogLoadedImages();

    // Generate a crash report with context
    void GenerateCrashReport(const char* aReason, void* aFaultAddress = nullptr);
}
}
#endif
