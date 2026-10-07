#include "StructuredLogging.hpp"

#ifdef RED4EXT_PLATFORM_MACOS
#include <algorithm>
#include <deque>
#include <fmt/format.h>
#include <iomanip>
#include <mutex>
#include <spdlog/logger.h>
#include <spdlog/spdlog.h>
#include <sstream>
#include <unordered_set>

// Simple JSON writer (no external dependency)
namespace SimpleJSON
{
inline std::string EscapeJSON(const std::string& str)
{
    std::string result;
    result.reserve(str.size() + 10);
    for (char c : str)
    {
        switch (c)
        {
        case '"':
            result += "\\\"";
            break;
        case '\\':
            result += "\\\\";
            break;
        case '\b':
            result += "\\b";
            break;
        case '\f':
            result += "\\f";
            break;
        case '\n':
            result += "\\n";
            break;
        case '\r':
            result += "\\r";
            break;
        case '\t':
            result += "\\t";
            break;
        default:
            if (c >= 0 && c < 32)
            {
                result += fmt::format("\\u{:04x}", static_cast<unsigned char>(c));
            }
            else
            {
                result += c;
            }
            break;
        }
    }
    return result;
}

inline std::string ToJSON(const std::string& key, const std::string& value)
{
    return fmt::format("\"{}\":\"{}\"", EscapeJSON(key), EscapeJSON(value));
}

inline std::string ToJSON(const std::string& key, uint64_t value)
{
    return fmt::format("\"{}\":{}", EscapeJSON(key), value);
}

inline std::string ToJSON(const std::string& key, bool value)
{
    return fmt::format("\"{}\":{}", EscapeJSON(key), value ? "true" : "false");
}
} // namespace SimpleJSON

namespace Platform
{
namespace StructuredLogging
{
namespace
{
std::mutex g_logMutex;
std::deque<LogEntry> g_logBuffer;
std::vector<LogContext> g_contextStack;
bool g_jsonExportEnabled = false;
bool g_structuredFormatEnabled = true;
std::string g_jsonLogPath;
std::unique_ptr<std::ofstream> g_jsonLogFile;
uint64_t g_sequenceNumber = 0;
std::chrono::steady_clock::time_point g_startTime = std::chrono::steady_clock::now();

constexpr size_t kMaxLogBufferSize = 50000; // Keep last 50k entries

std::string FormatTimestamp(const std::chrono::steady_clock::time_point& tp)
{
    auto duration = tp.time_since_epoch();
    auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
    return fmt::format("{}", millis);
}

std::string FormatThreadId(std::thread::id tid)
{
    std::ostringstream oss;
    oss << tid;
    return oss.str();
}

std::string FormatPointer(void* ptr)
{
    if (!ptr)
    {
        return "nullptr";
    }
    return fmt::format("{:#018x}", reinterpret_cast<uintptr_t>(ptr));
}

std::string LevelToString(spdlog::level::level_enum level)
{
    switch (level)
    {
    case spdlog::level::trace:
        return "TRACE";
    case spdlog::level::debug:
        return "DEBUG";
    case spdlog::level::info:
        return "INFO";
    case spdlog::level::warn:
        return "WARN";
    case spdlog::level::err:
        return "ERROR";
    case spdlog::level::critical:
        return "CRITICAL";
    default:
        return "UNKNOWN";
    }
}

void WriteJSONEntry(const LogEntry& entry)
{
    if (!g_jsonExportEnabled || !g_jsonLogFile || !g_jsonLogFile->is_open())
    {
        return;
    }

    std::ostringstream oss;
    oss << "{\n";
    oss << "  " << SimpleJSON::ToJSON("sequence", entry.sequenceNumber) << ",\n";
    oss << "  " << SimpleJSON::ToJSON("timestamp", FormatTimestamp(entry.timestamp)) << ",\n";
    oss << "  " << SimpleJSON::ToJSON("level", LevelToString(entry.level)) << ",\n";
    oss << "  " << SimpleJSON::ToJSON("category", entry.category) << ",\n";
    oss << "  " << SimpleJSON::ToJSON("message", entry.message) << ",\n";
    oss << "  " << SimpleJSON::ToJSON("thread_id", FormatThreadId(entry.threadId)) << ",\n";
    oss << "  \"context\":{\n";
    oss << "    " << SimpleJSON::ToJSON("operation", entry.context.operation) << ",\n";
    oss << "    " << SimpleJSON::ToJSON("component", entry.context.component) << ",\n";
    oss << "    " << SimpleJSON::ToJSON("plugin", entry.context.plugin) << ",\n";
    oss << "    " << SimpleJSON::ToJSON("target", FormatPointer(entry.context.target)) << ",\n";
    std::string hashStr = entry.context.hash ? fmt::format("{:#08x}", entry.context.hash) : "";
    oss << "    " << SimpleJSON::ToJSON("hash", hashStr) << ",\n";
    oss << "    " << SimpleJSON::ToJSON("thread_id", FormatThreadId(entry.context.threadId)) << ",\n";
    oss << "    " << SimpleJSON::ToJSON("timestamp", FormatTimestamp(entry.context.timestamp)) << "\n";
    oss << "  }\n";
    oss << "}";

    *g_jsonLogFile << oss.str() << "\n";
    g_jsonLogFile->flush();
}

std::string FormatStructuredMessage(const LogEntry& entry)
{
    std::ostringstream oss;

    // Sequence number for easy reference
    oss << "[#" << entry.sequenceNumber << "] ";

    // Timestamp (relative to start)
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(entry.timestamp - g_startTime).count();
    oss << "[+" << elapsed << "ms] ";

    // Level
    oss << "[" << LevelToString(entry.level) << "] ";

    // Category
    oss << entry.category << " ";

    // Context info
    if (!entry.context.operation.empty() || !entry.context.component.empty())
    {
        oss << "{";
        if (!entry.context.operation.empty())
        {
            oss << "op:" << entry.context.operation;
        }
        if (!entry.context.component.empty())
        {
            if (!entry.context.operation.empty())
                oss << ",";
            oss << "comp:" << entry.context.component;
        }
        if (!entry.context.plugin.empty())
        {
            oss << ",plugin:" << entry.context.plugin;
        }
        if (entry.context.target)
        {
            oss << ",target:" << FormatPointer(entry.context.target);
        }
        if (entry.context.hash)
        {
            oss << ",hash:" << fmt::format("{:#08x}", entry.context.hash);
        }
        oss << "} ";
    }

    // Thread ID
    oss << "[tid:" << FormatThreadId(entry.threadId) << "] ";

    // Message
    oss << entry.message;

    return oss.str();
}
} // namespace

void Initialize(const std::string& aLogDir)
{
    std::lock_guard<std::mutex> lock(g_logMutex);
    g_startTime = std::chrono::steady_clock::now();
    g_sequenceNumber = 0;
    g_logBuffer.clear();
    g_contextStack.clear();

    LogMarker("SYSTEM_START", "RED4ext structured logging initialized");
}

void SetJSONExport(bool aEnabled, const std::string& aJsonLogPath)
{
    std::lock_guard<std::mutex> lock(g_logMutex);
    g_jsonExportEnabled = aEnabled;
    g_jsonLogPath = aJsonLogPath;

    if (aEnabled && !aJsonLogPath.empty())
    {
        g_jsonLogFile = std::make_unique<std::ofstream>(aJsonLogPath, std::ios::app);
        if (g_jsonLogFile->is_open())
        {
            LogMarker("JSON_EXPORT_START", "JSON log export enabled");
        }
    }
    else
    {
        if (g_jsonLogFile && g_jsonLogFile->is_open())
        {
            LogMarker("JSON_EXPORT_END", "JSON log export disabled");
            g_jsonLogFile->close();
        }
        g_jsonLogFile.reset();
    }
}

void SetStructuredFormat(bool aEnabled)
{
    std::lock_guard<std::mutex> lock(g_logMutex);
    g_structuredFormatEnabled = aEnabled;
}

void PushContext(const LogContext& aContext)
{
    std::lock_guard<std::mutex> lock(g_logMutex);
    g_contextStack.push_back(aContext);
}

void PushContext(const std::string& aOperation, const std::string& aComponent, const std::string& aPlugin,
                 void* aTarget, uint32_t aHash)
{
    LogContext ctx;
    ctx.operation = aOperation;
    ctx.component = aComponent;
    ctx.plugin = aPlugin;
    ctx.target = aTarget;
    ctx.hash = aHash;
    ctx.threadId = std::this_thread::get_id();
    ctx.timestamp = std::chrono::steady_clock::now();
    PushContext(ctx);
}

void PopContext()
{
    std::lock_guard<std::mutex> lock(g_logMutex);
    if (!g_contextStack.empty())
    {
        g_contextStack.pop_back();
    }
}

void ClearContext()
{
    std::lock_guard<std::mutex> lock(g_logMutex);
    g_contextStack.clear();
}

LogContext GetCurrentContext()
{
    std::lock_guard<std::mutex> lock(g_logMutex);
    if (!g_contextStack.empty())
    {
        return g_contextStack.back();
    }
    return LogContext{};
}

void LogStructured(spdlog::level::level_enum aLevel, const std::string& aCategory, const std::string& aMessage,
                   const LogContext* aOverrideContext)
{
    std::lock_guard<std::mutex> lock(g_logMutex);

    LogEntry entry;
    entry.level = aLevel;
    entry.category = aCategory;
    entry.message = aMessage;
    entry.timestamp = std::chrono::steady_clock::now();
    entry.threadId = std::this_thread::get_id();
    entry.sequenceNumber = ++g_sequenceNumber;

    // Use override context or current context stack
    if (aOverrideContext)
    {
        entry.context = *aOverrideContext;
    }
    else if (!g_contextStack.empty())
    {
        entry.context = g_contextStack.back();
    }

    // Add to buffer
    g_logBuffer.push_back(entry);
    if (g_logBuffer.size() > kMaxLogBufferSize)
    {
        g_logBuffer.pop_front();
    }

    // Write to JSON if enabled
    WriteJSONEntry(entry);

    // Log to spdlog with structured format
    std::string formattedMsg = FormatStructuredMessage(entry);

    switch (aLevel)
    {
    case spdlog::level::trace:
        spdlog::trace("{}", formattedMsg);
        break;
    case spdlog::level::debug:
        spdlog::debug("{}", formattedMsg);
        break;
    case spdlog::level::info:
        spdlog::info("{}", formattedMsg);
        break;
    case spdlog::level::warn:
        spdlog::warn("{}", formattedMsg);
        break;
    case spdlog::level::err:
        spdlog::error("{}", formattedMsg);
        break;
    case spdlog::level::critical:
        spdlog::critical("{}", formattedMsg);
        break;
    default:
        spdlog::info("{}", formattedMsg);
        break;
    }
}

void LogMarker(const std::string& aMarkerName, const std::string& aDescription)
{
    std::string msg = fmt::format("=== MARKER: {} ===", aMarkerName);
    if (!aDescription.empty())
    {
        msg += fmt::format(" - {}", aDescription);
    }
    LogStructured(spdlog::level::info, "[MARKER]", msg);
}

void LogSectionStart(const std::string& aSectionName)
{
    LogStructured(spdlog::level::info, "[SECTION]", fmt::format(">>> START: {} <<<", aSectionName));
}

void LogSectionEnd(const std::string& aSectionName)
{
    LogStructured(spdlog::level::info, "[SECTION]", fmt::format(">>> END: {} <<<", aSectionName));
}

bool ExportToJSON(const std::string& aFilePath, size_t aMaxEntries)
{
    std::lock_guard<std::mutex> lock(g_logMutex);

    try
    {
        std::ofstream file(aFilePath);
        if (!file.is_open())
        {
            return false;
        }

        file << "[\n";

        size_t count = 0;
        size_t startIdx = g_logBuffer.size() > aMaxEntries ? g_logBuffer.size() - aMaxEntries : 0;

        for (size_t i = startIdx; i < g_logBuffer.size(); ++i)
        {
            const auto& entry = g_logBuffer[i];

            if (count > 0)
            {
                file << ",\n";
            }

            std::ostringstream oss;
            oss << "  {\n";
            oss << "    " << SimpleJSON::ToJSON("sequence", entry.sequenceNumber) << ",\n";
            oss << "    " << SimpleJSON::ToJSON("timestamp", FormatTimestamp(entry.timestamp)) << ",\n";
            oss << "    " << SimpleJSON::ToJSON("level", LevelToString(entry.level)) << ",\n";
            oss << "    " << SimpleJSON::ToJSON("category", entry.category) << ",\n";
            oss << "    " << SimpleJSON::ToJSON("message", entry.message) << ",\n";
            oss << "    " << SimpleJSON::ToJSON("thread_id", FormatThreadId(entry.threadId)) << ",\n";
            oss << "    \"context\":{\n";
            oss << "      " << SimpleJSON::ToJSON("operation", entry.context.operation) << ",\n";
            oss << "      " << SimpleJSON::ToJSON("component", entry.context.component) << ",\n";
            oss << "      " << SimpleJSON::ToJSON("plugin", entry.context.plugin) << ",\n";
            oss << "      " << SimpleJSON::ToJSON("target", FormatPointer(entry.context.target)) << ",\n";
            std::string hashStr = entry.context.hash ? fmt::format("{:#08x}", entry.context.hash) : "";
            oss << "      " << SimpleJSON::ToJSON("hash", hashStr) << ",\n";
            oss << "      " << SimpleJSON::ToJSON("thread_id", FormatThreadId(entry.context.threadId)) << ",\n";
            oss << "      " << SimpleJSON::ToJSON("timestamp", FormatTimestamp(entry.context.timestamp)) << "\n";
            oss << "    }\n";
            oss << "  }";

            file << oss.str();
            count++;
        }

        file << "\n]\n";
        return true;
    }
    catch (...)
    {
        return false;
    }
}

LogSummary GenerateSummary()
{
    std::lock_guard<std::mutex> lock(g_logMutex);

    LogSummary summary;
    summary.startTime = g_startTime;
    summary.endTime = std::chrono::steady_clock::now();
    summary.totalEntries = g_logBuffer.size();

    std::unordered_map<std::string, LogSummary::ErrorSummary> errorMap;
    std::unordered_map<std::string, LogSummary::WarningSummary> warningMap;
    std::unordered_set<std::string> operations;
    std::unordered_set<std::string> components;

    for (const auto& entry : g_logBuffer)
    {
        if (entry.level == spdlog::level::err || entry.level == spdlog::level::critical)
        {
            summary.errorCount++;
            std::string key = entry.category + "::" + entry.message;
            auto& err = errorMap[key];
            err.category = entry.category;
            err.message = entry.message;
            err.count++;
            err.sequenceNumbers.push_back(entry.sequenceNumber);
        }
        else if (entry.level == spdlog::level::warn)
        {
            summary.warningCount++;
            std::string key = entry.category + "::" + entry.message;
            auto& warn = warningMap[key];
            warn.category = entry.category;
            warn.message = entry.message;
            warn.count++;
        }

        if (!entry.context.operation.empty())
        {
            operations.insert(entry.context.operation);
        }
        if (!entry.context.component.empty())
        {
            components.insert(entry.context.component);
        }
    }

    summary.errors.reserve(errorMap.size());
    for (const auto& [key, err] : errorMap)
    {
        summary.errors.push_back(err);
    }

    summary.warnings.reserve(warningMap.size());
    for (const auto& [key, warn] : warningMap)
    {
        summary.warnings.push_back(warn);
    }
    summary.operations.assign(operations.begin(), operations.end());
    summary.components.assign(components.begin(), components.end());

    return summary;
}

bool ExportSummaryToJSON(const std::string& aFilePath)
{
    auto summary = GenerateSummary();

    try
    {
        std::ofstream file(aFilePath);
        if (!file.is_open())
        {
            return false;
        }

        file << "{\n";
        file << "  " << SimpleJSON::ToJSON("total_entries", summary.totalEntries) << ",\n";
        file << "  " << SimpleJSON::ToJSON("error_count", summary.errorCount) << ",\n";
        file << "  " << SimpleJSON::ToJSON("warning_count", summary.warningCount) << ",\n";
        file << "  " << SimpleJSON::ToJSON("start_time", FormatTimestamp(summary.startTime)) << ",\n";
        file << "  " << SimpleJSON::ToJSON("end_time", FormatTimestamp(summary.endTime)) << ",\n";

        file << "  \"errors\":[\n";
        for (size_t i = 0; i < summary.errors.size(); ++i)
        {
            const auto& err = summary.errors[i];
            if (i > 0)
                file << ",\n";
            file << "    {\n";
            file << "      " << SimpleJSON::ToJSON("category", err.category) << ",\n";
            file << "      " << SimpleJSON::ToJSON("message", err.message) << ",\n";
            file << "      " << SimpleJSON::ToJSON("count", static_cast<uint64_t>(err.count)) << ",\n";
            file << "      \"sequence_numbers\":[";
            for (size_t j = 0; j < err.sequenceNumbers.size(); ++j)
            {
                if (j > 0)
                    file << ",";
                file << err.sequenceNumbers[j];
            }
            file << "]\n";
            file << "    }";
        }
        file << "\n  ],\n";

        file << "  \"warnings\":[\n";
        for (size_t i = 0; i < summary.warnings.size(); ++i)
        {
            const auto& warn = summary.warnings[i];
            if (i > 0)
                file << ",\n";
            file << "    {\n";
            file << "      " << SimpleJSON::ToJSON("category", warn.category) << ",\n";
            file << "      " << SimpleJSON::ToJSON("message", warn.message) << ",\n";
            file << "      " << SimpleJSON::ToJSON("count", static_cast<uint64_t>(warn.count)) << "\n";
            file << "    }";
        }
        file << "\n  ],\n";

        file << "  \"operations\":[";
        for (size_t i = 0; i < summary.operations.size(); ++i)
        {
            if (i > 0)
                file << ",";
            file << "\"" << SimpleJSON::EscapeJSON(summary.operations[i]) << "\"";
        }
        file << "],\n";

        file << "  \"components\":[";
        for (size_t i = 0; i < summary.components.size(); ++i)
        {
            if (i > 0)
                file << ",";
            file << "\"" << SimpleJSON::EscapeJSON(summary.components[i]) << "\"";
        }
        file << "]\n";

        file << "}\n";
        return true;
    }
    catch (...)
    {
        return false;
    }
}

void Shutdown()
{
    std::lock_guard<std::mutex> lock(g_logMutex);
    LogMarker("SYSTEM_END", "RED4ext structured logging shutting down");

    if (g_jsonLogFile && g_jsonLogFile->is_open())
    {
        g_jsonLogFile->close();
    }
    g_jsonLogFile.reset();
}
} // namespace StructuredLogging
} // namespace Platform
#endif
