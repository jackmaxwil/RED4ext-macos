#pragma once

#include "Platform.hpp"

#include <RED4ext/GameStates.hpp>

#include <fmt/format.h>
#include <spdlog/logger.h>

#ifndef RED4EXT_PLATFORM_MACOS
#include <Windows.h>
#endif

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

class Config;
class DevConsole;
class Paths;

namespace Utils
{
std::shared_ptr<spdlog::logger> CreateLogger(const std::wstring_view aLogName, const std::wstring_view aFilename,
                                             const Paths& aPaths, const Config& aConfig, const DevConsole& aDevConsole);

std::wstring GetStateName(RED4ext::EGameStateType aStateType);

std::wstring FormatSystemMessage(uint32_t aMessageId);
std::wstring FormatLastError();
std::wstring FormatCurrentTimestamp();

int32_t ShowMessageBoxEx(const std::wstring_view aCaption, const std::wstring_view aText, uint32_t aType = MB_OK);
int32_t ShowMessageBox(const std::wstring_view aText, uint32_t aType = MB_OK);

std::string Narrow(const std::wstring_view aText);
std::wstring Widen(const std::string_view aText);

std::wstring ToLower(const std::wstring& acText);

template<typename... Args>
int32_t ShowMessageBox(uint32_t aType, const std::wstring_view aText, Args&&... aArgs)
{
    return ShowMessageBox(fmt::format(fmt::runtime(aText), std::forward<Args>(aArgs)...), aType);
}

template<typename... Args>
void ShowLastErrorMessage(uint32_t aType, const std::wstring_view aAdditionalText = L"", Args&&... aArgs)
{
    auto msg = FormatLastError();
    if (!aAdditionalText.empty())
    {
        msg += L"\n\n";
        msg += aAdditionalText;

        if constexpr (sizeof...(Args) > 0)
        {
            msg = fmt::format(fmt::runtime(msg), std::forward<Args>(aArgs)...);
        }
    }

    auto error = Platform::GetLastError();
    auto caption = fmt::format(L"RED4ext (error {})", error);
    ShowMessageBoxEx(caption.c_str(), msg.c_str(), aType);
}
} // namespace Utils

#ifdef RED4EXT_PLATFORM_MACOS
// On macOS, paths are always narrow strings
// Provide both char and wchar_t specializations for compatibility
template<>
struct fmt::formatter<std::filesystem::path, char> : formatter<std::string_view, char>
{
    template<typename FormatContext>
    auto format(const std::filesystem::path& path, FormatContext& ctx)
    {
        return formatter<std::string_view, char>::format(path.string(), ctx);
    }
};

template<>
struct fmt::formatter<std::filesystem::path, wchar_t> : formatter<std::wstring_view, wchar_t>
{
    template<typename FormatContext>
    auto format(const std::filesystem::path& path, FormatContext& ctx)
    {
        // Convert narrow path to wide for formatting
        return formatter<std::wstring_view, wchar_t>::format(Utils::Widen(path.string()), ctx);
    }
};
#else
template<typename Char>
struct fmt::formatter<std::filesystem::path, Char> : formatter<basic_string_view<Char>, Char>
{
    template<typename FormatContext>
    auto format(const std::filesystem::path& path, FormatContext& ctx) const
    {
        return formatter<basic_string_view<Char>, Char>::format(path.c_str(), ctx);
    }
};
#endif

#ifdef RED4EXT_PLATFORM_MACOS
// On macOS, use simple logging instead of message boxes
#define SHOW_LAST_ERROR_MESSAGE_FILE_LINE(additionalText, ...) Log::warn("Error at {}:{}", __FILE__, __LINE__)

#define SHOW_LAST_ERROR_MESSAGE_AND_EXIT_FILE_LINE(additionalText, ...)                                                \
    Log::error("Fatal error at {}:{}", __FILE__, __LINE__);                                                            \
    Platform::TerminateProcess()

#define SHOW_MESSAGE_BOX_FILE_LINE(type, msg, ...) Log::warn("Message at {}:{}", __FILE__, __LINE__)

#define SHOW_MESSAGE_BOX_AND_EXIT_FILE_LINE(msg, ...)                                                                  \
    Log::error("Fatal error at {}:{}", __FILE__, __LINE__);                                                            \
    Platform::TerminateProcess()

#else // Windows

#ifndef SHOW_LAST_ERROR_MESSAGE_FILE_LINE
#define SHOW_LAST_ERROR_MESSAGE_FILE_LINE(additionalText, ...)                                                         \
    Utils::ShowLastErrorMessage(MB_ICONWARNING | MB_OK, additionalText L"\n\n{}:{}", ##__VA_ARGS__, TEXT(__FILE__),    \
                                __LINE__)
#endif

#ifndef SHOW_LAST_ERROR_MESSAGE_AND_EXIT_FILE_LINE
#define SHOW_LAST_ERROR_MESSAGE_AND_EXIT_FILE_LINE(additionalText, ...)                                                \
    Utils::ShowLastErrorMessage(                                                                                       \
        MB_ICONERROR | MB_OK, additionalText L"\n\n{}:{}\n\nThe game will close now to prevent unexpected behavior.",  \
        ##__VA_ARGS__, TEXT(__FILE__), __LINE__);                                                                      \
    Platform::TerminateProcess()
#endif

#ifndef SHOW_MESSAGE_BOX_FILE_LINE
#define SHOW_MESSAGE_BOX_FILE_LINE(type, msg, ...)                                                                     \
    Utils::ShowMessageBox(type, msg L"\n\n{}:{}", ##__VA_ARGS__, TEXT(__FILE__), __LINE__)
#endif

#ifndef SHOW_MESSAGE_BOX_AND_EXIT_FILE_LINE
#define SHOW_MESSAGE_BOX_AND_EXIT_FILE_LINE(msg, ...)                                                                  \
    Utils::ShowMessageBox(MB_ICONERROR | MB_OK,                                                                        \
                          msg L"\n\n{}:{}\n\nThe game will close now to prevent unexpected behavior.", ##__VA_ARGS__,  \
                          TEXT(__FILE__), __LINE__);                                                                   \
    Platform::TerminateProcess()
#endif

#endif // RED4EXT_PLATFORM_MACOS
