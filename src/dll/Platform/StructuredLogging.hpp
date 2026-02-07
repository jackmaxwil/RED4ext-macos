#pragma once

#ifdef RED4EXT_PLATFORM_MACOS
#include <cstdint>
#include <string>
#include <string_view>
#include <chrono>
#include <thread>
#include <unordered_map>
#include <vector>
#include <memory>
#include <sstream>
#include <fstream>

namespace Platform
{
namespace StructuredLogging
{
    // Log context for tracking operations
    struct LogContext
    {
        std::string operation;      // e.g., "HookAttach", "PluginLoad"
        std::string component;       // e.g., "Hooking", "PluginSystem"
        std::string plugin;         // Plugin name if applicable
        void* target = nullptr;    // Hook target or relevant pointer
        uint32_t hash = 0;          // Address hash if applicable
        std::thread::id threadId = std::this_thread::get_id();
        std::chrono::steady_clock::time_point timestamp = std::chrono::steady_clock::now();
    };

    // Structured log entry
    struct LogEntry
    {
        spdlog::level::level_enum level;
        std::string category;        // e.g., "[RuntimeValidation]", "[Hooking]"
        std::string message;
        LogContext context;
        std::chrono::steady_clock::time_point timestamp;
        std::thread::id threadId;
        uint64_t sequenceNumber;
    };

    // Initialize structured logging
    void Initialize(const std::string& aLogDir);

    // Set JSON export mode
    void SetJSONExport(bool aEnabled, const std::string& aJsonLogPath = "");

    // Set structured format mode
    void SetStructuredFormat(bool aEnabled);

    // Push context (for operation tracking)
    void PushContext(const LogContext& aContext);
    void PushContext(const std::string& aOperation, const std::string& aComponent, 
                     const std::string& aPlugin = "", void* aTarget = nullptr, uint32_t aHash = 0);

    // Pop context
    void PopContext();

    // Clear all contexts
    void ClearContext();

    // Get current context
    LogContext GetCurrentContext();

    // Structured logging functions
    void LogStructured(spdlog::level::level_enum aLevel, const std::string& aCategory, 
                       const std::string& aMessage, const LogContext* aOverrideContext = nullptr);

    // Convenience macros for structured logging
    #define LOG_STRUCTURED_TRACE(category, msg) \
        Platform::StructuredLogging::LogStructured(spdlog::level::trace, category, msg)
    
    #define LOG_STRUCTURED_DEBUG(category, msg) \
        Platform::StructuredLogging::LogStructured(spdlog::level::debug, category, msg)
    
    #define LOG_STRUCTURED_INFO(category, msg) \
        Platform::StructuredLogging::LogStructured(spdlog::level::info, category, msg)
    
    #define LOG_STRUCTURED_WARN(category, msg) \
        Platform::StructuredLogging::LogStructured(spdlog::level::warn, category, msg)
    
    #define LOG_STRUCTURED_ERROR(category, msg) \
        Platform::StructuredLogging::LogStructured(spdlog::level::err, category, msg)
    
    #define LOG_STRUCTURED_CRITICAL(category, msg) \
        Platform::StructuredLogging::LogStructured(spdlog::level::critical, category, msg)

    // Log markers for section boundaries (easy to parse)
    void LogMarker(const std::string& aMarkerName, const std::string& aDescription = "");
    void LogSectionStart(const std::string& aSectionName);
    void LogSectionEnd(const std::string& aSectionName);

    // Export logs to JSON for AI parsing
    bool ExportToJSON(const std::string& aFilePath, size_t aMaxEntries = 10000);

    // Generate log summary for AI analysis
    struct LogSummary
    {
        struct ErrorSummary
        {
            std::string category;
            std::string message;
            uint32_t count = 0;
            std::vector<uint64_t> sequenceNumbers;
        };

        struct WarningSummary
        {
            std::string category;
            std::string message;
            uint32_t count = 0;
        };

        uint64_t totalEntries = 0;
        uint64_t errorCount = 0;
        uint64_t warningCount = 0;
        std::vector<ErrorSummary> errors;
        std::vector<WarningSummary> warnings;
        std::vector<std::string> operations;
        std::vector<std::string> components;
        std::chrono::steady_clock::time_point startTime;
        std::chrono::steady_clock::time_point endTime;
    };

    LogSummary GenerateSummary();

    // Export summary to JSON
    bool ExportSummaryToJSON(const std::string& aFilePath);

    // Shutdown structured logging
    void Shutdown();
}
}
#endif
