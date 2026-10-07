#pragma once

#include <spdlog/spdlog.h>
#include <string>
#include <type_traits>

/**
 * @brief Platform-agnostic logging wrapper for spdlog.
 *
 * On Windows, spdlog supports wide strings natively.
 * On macOS, we convert wide strings to narrow strings before logging.
 */
namespace Log
{

#ifdef RED4EXT_PLATFORM_MACOS

// Convert wide string to narrow string (simplified ASCII conversion)
inline std::string Narrow(const wchar_t* ws)
{
    if (!ws)
        return "";
    std::string result;
    result.reserve(128);
    while (*ws)
    {
        wchar_t c = *ws++;
        if (c < 128)
            result += static_cast<char>(c);
        else
            result += '?';
    }
    return result;
}

inline std::string Narrow(const std::wstring& ws)
{
    return Narrow(ws.c_str());
}

inline std::string Narrow(std::wstring_view ws)
{
    std::string result;
    result.reserve(ws.size());
    for (wchar_t c : ws)
    {
        if (c < 128)
            result += static_cast<char>(c);
        else
            result += '?';
    }
    return result;
}

// Pass-through for narrow strings
inline const std::string& Narrow(const std::string& s)
{
    return s;
}
inline std::string Narrow(std::string_view s)
{
    return std::string(s);
}
inline std::string Narrow(const char* s)
{
    return s ? s : "";
}

// Convert filesystem::path to string
inline std::string NarrowPath(const std::filesystem::path& p)
{
    return p.string();
}

namespace detail
{

// Helper to detect if T is a wide character type
template<typename T>
struct is_wide_char : std::false_type
{
};

template<>
struct is_wide_char<wchar_t*> : std::true_type
{
};

template<>
struct is_wide_char<const wchar_t*> : std::true_type
{
};

template<>
struct is_wide_char<std::wstring> : std::true_type
{
};

template<>
struct is_wide_char<std::wstring_view> : std::true_type
{
};

template<>
struct is_wide_char<std::wstring&> : std::true_type
{
};

template<>
struct is_wide_char<const std::wstring&> : std::true_type
{
};

// Convert a single argument: wide strings become narrow, others pass through
template<typename T>
auto ConvertArg(T&& arg)
{
    using DecayT = std::decay_t<T>;
    if constexpr (is_wide_char<DecayT>::value)
    {
        return Narrow(std::forward<T>(arg));
    }
    else if constexpr (std::is_same_v<DecayT, std::filesystem::path>)
    {
        return arg.string();
    }
    else
    {
        return std::forward<T>(arg);
    }
}

} // namespace detail

// Logging functions that convert wide format strings and arguments to narrow on macOS
// Note: We use fmt::runtime() because the format string is not a compile-time constant
//       after conversion from wide to narrow string.

template<typename... Args>
void trace(const wchar_t* fmt, Args&&... args)
{
    spdlog::trace(fmt::runtime(Narrow(fmt)), detail::ConvertArg(std::forward<Args>(args))...);
}

template<typename... Args>
void debug(const wchar_t* fmt, Args&&... args)
{
    spdlog::debug(fmt::runtime(Narrow(fmt)), detail::ConvertArg(std::forward<Args>(args))...);
}

template<typename... Args>
void info(const wchar_t* fmt, Args&&... args)
{
    spdlog::info(fmt::runtime(Narrow(fmt)), detail::ConvertArg(std::forward<Args>(args))...);
}

template<typename... Args>
void warn(const wchar_t* fmt, Args&&... args)
{
    spdlog::warn(fmt::runtime(Narrow(fmt)), detail::ConvertArg(std::forward<Args>(args))...);
}

template<typename... Args>
void error(const wchar_t* fmt, Args&&... args)
{
    spdlog::error(fmt::runtime(Narrow(fmt)), detail::ConvertArg(std::forward<Args>(args))...);
}

template<typename... Args>
void critical(const wchar_t* fmt, Args&&... args)
{
    spdlog::critical(fmt::runtime(Narrow(fmt)), detail::ConvertArg(std::forward<Args>(args))...);
}

// Overloads for narrow format strings - still need to convert wide arguments
template<typename... Args>
void trace(const char* fmt, Args&&... args)
{
    spdlog::trace(fmt::runtime(fmt), detail::ConvertArg(std::forward<Args>(args))...);
}

template<typename... Args>
void debug(const char* fmt, Args&&... args)
{
    spdlog::debug(fmt::runtime(fmt), detail::ConvertArg(std::forward<Args>(args))...);
}

template<typename... Args>
void info(const char* fmt, Args&&... args)
{
    spdlog::info(fmt::runtime(fmt), detail::ConvertArg(std::forward<Args>(args))...);
}

template<typename... Args>
void warn(const char* fmt, Args&&... args)
{
    spdlog::warn(fmt::runtime(fmt), detail::ConvertArg(std::forward<Args>(args))...);
}

template<typename... Args>
void error(const char* fmt, Args&&... args)
{
    spdlog::error(fmt::runtime(fmt), detail::ConvertArg(std::forward<Args>(args))...);
}

template<typename... Args>
void critical(const char* fmt, Args&&... args)
{
    spdlog::critical(fmt::runtime(fmt), detail::ConvertArg(std::forward<Args>(args))...);
}

// Single argument versions (for non-format logging)
inline void trace(const wchar_t* msg)
{
    spdlog::trace(Narrow(msg));
}
inline void debug(const wchar_t* msg)
{
    spdlog::debug(Narrow(msg));
}
inline void info(const wchar_t* msg)
{
    spdlog::info(Narrow(msg));
}
inline void warn(const wchar_t* msg)
{
    spdlog::warn(Narrow(msg));
}
inline void error(const wchar_t* msg)
{
    spdlog::error(Narrow(msg));
}
inline void critical(const wchar_t* msg)
{
    spdlog::critical(Narrow(msg));
}

inline void trace(const char* msg)
{
    spdlog::trace(msg);
}
inline void debug(const char* msg)
{
    spdlog::debug(msg);
}
inline void info(const char* msg)
{
    spdlog::info(msg);
}
inline void warn(const char* msg)
{
    spdlog::warn(msg);
}
inline void error(const char* msg)
{
    spdlog::error(msg);
}
inline void critical(const char* msg)
{
    spdlog::critical(msg);
}

#else // Windows - pass through to spdlog directly

using spdlog::critical;
using spdlog::debug;
using spdlog::error;
using spdlog::info;
using spdlog::trace;
using spdlog::warn;

#endif

} // namespace Log
