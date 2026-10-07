#include "Logger.hpp"
#include "App.hpp"
#include "stdafx.hpp"

#include <RED4ext/Api/v1/PluginHandle.hpp>
#include <fmt/format.h>

#include <cstdarg>
#include <cstdio>
#include <vector>

#ifdef RED4EXT_PLATFORM_MACOS
// macOS equivalents for Windows-specific printf functions
inline int macos_vscprintf(const char* format, va_list args)
{
    va_list args_copy;
    va_copy(args_copy, args);
    int len = vsnprintf(nullptr, 0, format, args_copy);
    va_end(args_copy);
    return len;
}

inline int macos_vsnprintf(char* buffer, size_t bufsize, size_t /*maxcount*/, const char* format, va_list args)
{
    return vsnprintf(buffer, bufsize, format, args);
}

inline int macos_vscwprintf(const wchar_t* format, va_list args)
{
    va_list args_copy;
    va_copy(args_copy, args);
    // Use vswprintf with a temp buffer to get length
    wchar_t temp[1];
    int len = vswprintf(temp, 0, format, args_copy);
    va_end(args_copy);
    // If vswprintf returns -1, try with a larger buffer estimation
    if (len < 0)
    {
        // Estimate: assume reasonable upper bound
        return 1024;
    }
    return len;
}

inline int macos_vsnwprintf(wchar_t* buffer, size_t bufsize, size_t /*maxcount*/, const wchar_t* format, va_list args)
{
    return vswprintf(buffer, bufsize, format, args);
}
#endif

#define Log(func)                                                                                                      \
    if (!aMessage)                                                                                                     \
    {                                                                                                                  \
        Log::warn("Plugin with handle {} tried to log a message with a NULL message", fmt::ptr(aHandle));              \
        return;                                                                                                        \
    }                                                                                                                  \
                                                                                                                       \
    auto app = App::Get();                                                                                             \
    if (!app)                                                                                                          \
    {                                                                                                                  \
        return;                                                                                                        \
    }                                                                                                                  \
                                                                                                                       \
    auto pluginSystem = app->GetPluginSystem();                                                                        \
    auto plugin = pluginSystem->GetPlugin(aHandle);                                                                    \
    if (!plugin)                                                                                                       \
    {                                                                                                                  \
        return;                                                                                                        \
    }                                                                                                                  \
    auto loggerSystem = app->GetLoggerSystem();                                                                        \
    loggerSystem->func(plugin, aMessage)

#define LogF(char_type, count_fn, format_fn, func)                                                                     \
    if (!aFormat)                                                                                                      \
    {                                                                                                                  \
        Log::warn("Plugin with handle {} tried to log a message with a NULL format", fmt::ptr(aHandle));               \
        return;                                                                                                        \
    }                                                                                                                  \
                                                                                                                       \
    auto app = App::Get();                                                                                             \
    if (!app)                                                                                                          \
    {                                                                                                                  \
        return;                                                                                                        \
    }                                                                                                                  \
                                                                                                                       \
    auto pluginSystem = app->GetPluginSystem();                                                                        \
    auto plugin = pluginSystem->GetPlugin(aHandle);                                                                    \
    if (!plugin)                                                                                                       \
    {                                                                                                                  \
        return;                                                                                                        \
    }                                                                                                                  \
                                                                                                                       \
    va_list args;                                                                                                      \
    va_start(args, aFormat);                                                                                           \
                                                                                                                       \
    auto len = count_fn(aFormat, args);                                                                                \
    if (len > 0)                                                                                                       \
    {                                                                                                                  \
        std::vector<char_type> buffer(len + 1); /* len + NULL character. */                                            \
                                                                                                                       \
        auto res = format_fn(buffer.data(), buffer.size(), buffer.size() - 1, aFormat, args);                          \
        if (res > 0)                                                                                                   \
        {                                                                                                              \
            auto loggerSystem = app->GetLoggerSystem();                                                                \
            loggerSystem->func(plugin, {buffer.data(), buffer.size() - 1}); /* Exclude NULL character. */              \
        }                                                                                                              \
        else if (res < 0)                                                                                              \
        {                                                                                                              \
            Log::warn(L"Could not format the log message logged by '{}'. '" #format_fn "' returned {}",                \
                      plugin->GetName(), res);                                                                         \
        }                                                                                                              \
    }                                                                                                                  \
    else if (len < 0)                                                                                                  \
    {                                                                                                                  \
        Log::warn(L"Could not get the length of the formatted log message logged by '{}'. '" #count_fn                 \
                  "' returned {}",                                                                                     \
                  plugin->GetName(), len);                                                                             \
    }                                                                                                                  \
                                                                                                                       \
    va_end(args)

void v1::Logger::Trace(RED4ext::v1::PluginHandle aHandle, const char* aMessage)
{
    Log(Trace);
}

void v1::Logger::TraceF(RED4ext::v1::PluginHandle aHandle, const char* aFormat, ...)
{
#ifdef RED4EXT_PLATFORM_MACOS
    LogF(char, macos_vscprintf, macos_vsnprintf, Trace);
#else
    LogF(char, ::_vscprintf, ::vsnprintf_s, Trace);
#endif
}

void v1::Logger::TraceW(RED4ext::v1::PluginHandle aHandle, const wchar_t* aMessage)
{
    Log(Trace);
}

void v1::Logger::TraceWF(RED4ext::v1::PluginHandle aHandle, const wchar_t* aFormat, ...)
{
#ifdef RED4EXT_PLATFORM_MACOS
    LogF(wchar_t, macos_vscwprintf, macos_vsnwprintf, Trace);
#else
    LogF(wchar_t, ::_vscwprintf, ::_vsnwprintf_s, Trace);
#endif
}

void v1::Logger::Debug(RED4ext::v1::PluginHandle aHandle, const char* aMessage)
{
    Log(Debug);
}

void v1::Logger::DebugF(RED4ext::v1::PluginHandle aHandle, const char* aFormat, ...)
{
#ifdef RED4EXT_PLATFORM_MACOS
    LogF(char, macos_vscprintf, macos_vsnprintf, Debug);
#else
    LogF(char, ::_vscprintf, ::vsnprintf_s, Debug);
#endif
}

void v1::Logger::DebugW(RED4ext::v1::PluginHandle aHandle, const wchar_t* aMessage)
{
    Log(Debug);
}

void v1::Logger::DebugWF(RED4ext::v1::PluginHandle aHandle, const wchar_t* aFormat, ...)
{
#ifdef RED4EXT_PLATFORM_MACOS
    LogF(wchar_t, macos_vscwprintf, macos_vsnwprintf, Debug);
#else
    LogF(wchar_t, ::_vscwprintf, ::_vsnwprintf_s, Debug);
#endif
}

void v1::Logger::Info(RED4ext::v1::PluginHandle aHandle, const char* aMessage)
{
    Log(Info);
}

void v1::Logger::InfoF(RED4ext::v1::PluginHandle aHandle, const char* aFormat, ...)
{
#ifdef RED4EXT_PLATFORM_MACOS
    LogF(char, macos_vscprintf, macos_vsnprintf, Info);
#else
    LogF(char, ::_vscprintf, ::vsnprintf_s, Info);
#endif
}

void v1::Logger::InfoW(RED4ext::v1::PluginHandle aHandle, const wchar_t* aMessage)
{
    Log(Info);
}

void v1::Logger::InfoWF(RED4ext::v1::PluginHandle aHandle, const wchar_t* aFormat, ...)
{
#ifdef RED4EXT_PLATFORM_MACOS
    LogF(wchar_t, macos_vscwprintf, macos_vsnwprintf, Info);
#else
    LogF(wchar_t, ::_vscwprintf, ::_vsnwprintf_s, Info);
#endif
}

void v1::Logger::Warn(RED4ext::v1::PluginHandle aHandle, const char* aMessage)
{
    Log(Warn);
}

void v1::Logger::WarnF(RED4ext::v1::PluginHandle aHandle, const char* aFormat, ...)
{
#ifdef RED4EXT_PLATFORM_MACOS
    LogF(char, macos_vscprintf, macos_vsnprintf, Warn);
#else
    LogF(char, ::_vscprintf, ::vsnprintf_s, Warn);
#endif
}

void v1::Logger::WarnW(RED4ext::v1::PluginHandle aHandle, const wchar_t* aMessage)
{
    Log(Warn);
}

void v1::Logger::WarnWF(RED4ext::v1::PluginHandle aHandle, const wchar_t* aFormat, ...)
{
#ifdef RED4EXT_PLATFORM_MACOS
    LogF(wchar_t, macos_vscwprintf, macos_vsnwprintf, Warn);
#else
    LogF(wchar_t, ::_vscwprintf, ::_vsnwprintf_s, Warn);
#endif
}

void v1::Logger::Error(RED4ext::v1::PluginHandle aHandle, const char* aMessage)
{
    Log(Error);
}

void v1::Logger::ErrorF(RED4ext::v1::PluginHandle aHandle, const char* aFormat, ...)
{
#ifdef RED4EXT_PLATFORM_MACOS
    LogF(char, macos_vscprintf, macos_vsnprintf, Error);
#else
    LogF(char, ::_vscprintf, ::vsnprintf_s, Error);
#endif
}

void v1::Logger::ErrorW(RED4ext::v1::PluginHandle aHandle, const wchar_t* aMessage)
{
    Log(Error);
}

void v1::Logger::ErrorWF(RED4ext::v1::PluginHandle aHandle, const wchar_t* aFormat, ...)
{
#ifdef RED4EXT_PLATFORM_MACOS
    LogF(wchar_t, macos_vscwprintf, macos_vsnwprintf, Error);
#else
    LogF(wchar_t, ::_vscwprintf, ::_vsnwprintf_s, Error);
#endif
}

void v1::Logger::Critical(RED4ext::v1::PluginHandle aHandle, const char* aMessage)
{
    Log(Critical);
}

void v1::Logger::CriticalF(RED4ext::v1::PluginHandle aHandle, const char* aFormat, ...)
{
#ifdef RED4EXT_PLATFORM_MACOS
    LogF(char, macos_vscprintf, macos_vsnprintf, Critical);
#else
    LogF(char, ::_vscprintf, ::vsnprintf_s, Critical);
#endif
}

void v1::Logger::CriticalW(RED4ext::v1::PluginHandle aHandle, const wchar_t* aMessage)
{
    Log(Critical);
}

void v1::Logger::CriticalWF(RED4ext::v1::PluginHandle aHandle, const wchar_t* aFormat, ...)
{
#ifdef RED4EXT_PLATFORM_MACOS
    LogF(wchar_t, macos_vscwprintf, macos_vsnwprintf, Critical);
#else
    LogF(wchar_t, ::_vscwprintf, ::_vsnwprintf_s, Critical);
#endif
}
