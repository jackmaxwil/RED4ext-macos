#pragma once

#ifdef RED4EXT_PLATFORM_MACOS
#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace Platform
{
namespace PluginMonitor
{
struct PluginHealth
{
    std::string name;
    bool isLoaded = false;
    bool hasErrors = false;
    uint32_t hookFailures = 0;
    std::chrono::steady_clock::time_point loadTime;
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
};

// Track plugin load
void RecordPluginLoad(const std::string& aName);

// Track plugin unload
void RecordPluginUnload(const std::string& aName);

// Record plugin error
void RecordPluginError(const std::string& aName, const std::string& aError);

// Record plugin warning
void RecordPluginWarning(const std::string& aName, const std::string& aWarning);

// Record hook failure for a plugin
void RecordPluginHookFailure(const std::string& aName);

// Get health status for a plugin
PluginHealth GetPluginHealth(const std::string& aName);

// Get all plugin health statuses
std::vector<PluginHealth> GetAllPluginHealth();

// Generate plugin health report
std::string GenerateHealthReport();

// Check for problematic plugins
std::vector<std::string> GetProblematicPlugins();
} // namespace PluginMonitor
} // namespace Platform
#endif
