#include "PluginMonitor.hpp"

#ifdef RED4EXT_PLATFORM_MACOS
#include <unordered_map>
#include <algorithm>
#include <sstream>
#include <spdlog/spdlog.h>
#include <fmt/format.h>

namespace Platform
{
namespace PluginMonitor
{
namespace
{
std::mutex g_pluginHealthMutex;
std::unordered_map<std::string, PluginHealth> g_pluginHealth;
}

void RecordPluginLoad(const std::string& aName)
{
    std::lock_guard<std::mutex> lock(g_pluginHealthMutex);
    auto& health = g_pluginHealth[aName];
    health.name = aName;
    health.isLoaded = true;
    health.loadTime = std::chrono::steady_clock::now();
    health.hasErrors = false;
    health.errors.clear();
    health.warnings.clear();
    health.hookFailures = 0;
}

void RecordPluginUnload(const std::string& aName)
{
    std::lock_guard<std::mutex> lock(g_pluginHealthMutex);
    auto it = g_pluginHealth.find(aName);
    if (it != g_pluginHealth.end())
    {
        it->second.isLoaded = false;
    }
}

void RecordPluginError(const std::string& aName, const std::string& aError)
{
    std::lock_guard<std::mutex> lock(g_pluginHealthMutex);
    auto& health = g_pluginHealth[aName];
    health.name = aName;
    health.hasErrors = true;
    health.errors.push_back(aError);
    
    spdlog::error("[PluginMonitor] Plugin '{}' error: {}", aName, aError);
}

void RecordPluginWarning(const std::string& aName, const std::string& aWarning)
{
    std::lock_guard<std::mutex> lock(g_pluginHealthMutex);
    auto& health = g_pluginHealth[aName];
    health.name = aName;
    health.warnings.push_back(aWarning);
    
    spdlog::warn("[PluginMonitor] Plugin '{}' warning: {}", aName, aWarning);
}

void RecordPluginHookFailure(const std::string& aName)
{
    std::lock_guard<std::mutex> lock(g_pluginHealthMutex);
    auto& health = g_pluginHealth[aName];
    health.name = aName;
    health.hookFailures++;
    health.hasErrors = true;
    
    spdlog::warn("[PluginMonitor] Plugin '{}' hook failure (total: {})", aName, health.hookFailures);
}

PluginHealth GetPluginHealth(const std::string& aName)
{
    std::lock_guard<std::mutex> lock(g_pluginHealthMutex);
    auto it = g_pluginHealth.find(aName);
    if (it != g_pluginHealth.end())
    {
        return it->second;
    }
    
    PluginHealth health;
    health.name = aName;
    return health;
}

std::vector<PluginHealth> GetAllPluginHealth()
{
    std::lock_guard<std::mutex> lock(g_pluginHealthMutex);
    std::vector<PluginHealth> result;
    result.reserve(g_pluginHealth.size());
    
    for (const auto& [name, health] : g_pluginHealth)
    {
        result.push_back(health);
    }
    
    return result;
}

std::string GenerateHealthReport()
{
    std::lock_guard<std::mutex> lock(g_pluginHealthMutex);
    
    std::ostringstream oss;
    oss << "Plugin Health Report\n";
    oss << "====================\n\n";
    
    if (g_pluginHealth.empty())
    {
        oss << "No plugins loaded.\n";
        return oss.str();
    }
    
    for (const auto& [name, health] : g_pluginHealth)
    {
        oss << "Plugin: " << health.name << "\n";
        oss << "  Loaded: " << (health.isLoaded ? "Yes" : "No") << "\n";
        oss << "  Status: " << (health.hasErrors ? "ERRORS" : "OK") << "\n";
        oss << "  Hook Failures: " << health.hookFailures << "\n";
        
        if (!health.errors.empty())
        {
            oss << "  Errors:\n";
            for (const auto& error : health.errors)
            {
                oss << "    - " << error << "\n";
            }
        }
        
        if (!health.warnings.empty())
        {
            oss << "  Warnings:\n";
            for (const auto& warning : health.warnings)
            {
                oss << "    - " << warning << "\n";
            }
        }
        
        oss << "\n";
    }
    
    return oss.str();
}

std::vector<std::string> GetProblematicPlugins()
{
    std::lock_guard<std::mutex> lock(g_pluginHealthMutex);
    std::vector<std::string> problematic;
    
    for (const auto& [name, health] : g_pluginHealth)
    {
        if (health.hasErrors || health.hookFailures > 0)
        {
            problematic.push_back(name);
        }
    }
    
    return problematic;
}
}
}
#endif
