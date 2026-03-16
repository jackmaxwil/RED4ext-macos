#include "Utils.hpp"
#include "Config.hpp"
#include "DevConsole.hpp"
#include "Paths.hpp"
#include "Platform.hpp"
#include "stdafx.hpp"

#include <ctime>
#include <cwctype>

#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#ifdef RED4EXT_PLATFORM_MACOS
#include <cstdint>
#endif

std::shared_ptr<spdlog::logger> Utils::CreateLogger(const std::wstring_view aLogName, const std::wstring_view aFilename,
                                                    const Paths& aPaths, const Config& aConfig,
                                                    const DevConsole& aDevConsole)
{
    try
    {
        auto dir = aPaths.GetLogsDir();

        std::error_code err;
        auto exists = std::filesystem::exists(dir, err);
        if (err)
        {
            auto errVal = err.value();
            const auto& category = err.category();
            auto msg = category.message(errVal);

            SHOW_MESSAGE_BOX_AND_EXIT_FILE_LINE(
                L"An error occurred while checking logs directory existence:\n{}\n\nDirectory: {}", Utils::Widen(msg),
                dir);

            return nullptr;
        }

        if (!exists)
        {
            std::filesystem::create_directories(dir, err);
            if (err)
            {
                auto errVal = err.value();
                const auto& category = err.category();
                auto msg = category.message(errVal);

                SHOW_MESSAGE_BOX_AND_EXIT_FILE_LINE(
                    L"An error occurred while creating the logs directory:\n{}\n\nDirectory: {}", Utils::Widen(msg),
                    dir);

                return nullptr;
            }
        }

        constexpr auto oneByte = 1;
        constexpr auto oneKbInB = 1024 * oneByte;
        constexpr auto oneMbInB = 1024 * oneKbInB;

        const auto& loggingConfig = aConfig.GetLogging();
        size_t maxFiles = loggingConfig.maxFiles;
        size_t maxFileSize = static_cast<size_t>(loggingConfig.maxFileSize) * oneMbInB;

        auto file = dir / aFilename;
        auto logger = spdlog::rotating_logger_mt(Narrow(aLogName), file, maxFileSize, maxFiles, true);
        logger->set_level(loggingConfig.level);
        logger->flush_on(loggingConfig.flushOn);

        const auto& dev = aConfig.GetDev();
        if (dev.hasConsole && aDevConsole.IsOutputRedirected())
        {
            auto consoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
            logger->sinks().push_back(consoleSink);
        }

        return logger;
    }
    catch (const std::exception& e)
    {
        std::string_view msg = e.what();
        if (msg.starts_with("rotating_file_sink: failed renaming"))
        {
            SHOW_MESSAGE_BOX_AND_EXIT_FILE_LINE(
                L"Unable to rotate the log file. Please ensure that the game is completely closed and not running in "
                L"the background.\n\n{}",
                Utils::Widen(msg));
        }
        else
        {
            SHOW_MESSAGE_BOX_AND_EXIT_FILE_LINE(L"An exception occurred while creating the logger:\n{}",
                                                Utils::Widen(msg));
        }
    }

    return nullptr;
}

std::wstring Utils::GetStateName(RED4ext::EGameStateType aStateType)
{
    using enum RED4ext::EGameStateType;
    switch (aStateType)
    {
    case BaseInitialization:
    {
        return L"BaseInitialization";
    }
    case Initialization:
    {
        return L"Initialization";
    }
    case Running:
    {
        return L"Running";
    }
    case Shutdown:
    {
        return L"Shutdown";
    }
    default:
    {
        return L"unknown";
    }
    }
}

std::wstring Utils::FormatSystemMessage(uint32_t aMessageId)
{
#ifdef RED4EXT_PLATFORM_MACOS
    return fmt::format(L"System message for id {}", aMessageId);
#else
    wil::last_error_context last_error;
    wil::unique_hlocal_ptr<wchar_t> buffer;

    auto len =
        FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                      nullptr, aMessageId, LANG_USER_DEFAULT, wil::out_param_ptr<LPWSTR>(buffer), 0, nullptr);
    if (!len)
    {
        return fmt::format(L"Could not format the system message for the specified message id ({}), error code: {}",
                           aMessageId, Platform::GetLastError());
    }

    std::wstring_view res = buffer.get();

    // Remove the new lines at the end of the message, they are annoying.
    if (res.ends_with(L'\n'))
    {
        res.remove_suffix(1);
    }

    if (res.ends_with(L'\r'))
    {
        res.remove_suffix(1);
    }

    return std::wstring(res);
#endif
}

std::wstring Utils::FormatLastError()
{
    auto err = Platform::GetLastError();
    return FormatSystemMessage(err);
}

std::wstring Utils::FormatCurrentTimestamp()
{
    auto now = std::chrono::system_clock::now();
    std::time_t now_c = std::chrono::system_clock::to_time_t(now);

    // Convert to std::tm for formatting
    std::tm now_tm;
#ifdef RED4EXT_PLATFORM_MACOS
    localtime_r(&now_c, &now_tm);
#else
    localtime_s(&now_tm, &now_c);
#endif

    return fmt::format(L"{:04d}-{:02d}-{:02d}-{:02d}-{:02d}-{:02d}", now_tm.tm_year + 1900, now_tm.tm_mon + 1,
                       now_tm.tm_mday, now_tm.tm_hour, now_tm.tm_min, now_tm.tm_sec);
}

int32_t Utils::ShowMessageBoxEx(const std::wstring_view aCaption, const std::wstring_view aText, uint32_t aType)
{
    Platform::ShowMessageBox(aCaption.data(), aText.data(), aType);
    return 0;
}

int32_t Utils::ShowMessageBox(const std::wstring_view aText, uint32_t aType)
{
    return ShowMessageBoxEx(L"RED4ext", aText, aType);
}

std::string Utils::Narrow(const std::wstring_view aText)
{
    if (aText.empty())
    {
        return "";
    }

    std::string result;

#ifdef RED4EXT_PLATFORM_MACOS
    for (wchar_t wc : aText)
    {
        const std::uint32_t cp = static_cast<std::uint32_t>(wc);
        if (cp <= 0x7F)
        {
            result.push_back(static_cast<char>(cp));
        }
        else if (cp <= 0x7FF)
        {
            result.push_back(static_cast<char>(0xC0 | ((cp >> 6) & 0x1F)));
            result.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
        else if (cp <= 0xFFFF)
        {
            result.push_back(static_cast<char>(0xE0 | ((cp >> 12) & 0x0F)));
            result.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            result.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
        else if (cp <= 0x10FFFF)
        {
            result.push_back(static_cast<char>(0xF0 | ((cp >> 18) & 0x07)));
            result.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            result.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            result.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
        else
        {
            result.push_back('?');
        }
    }
#else
    auto len =
        WideCharToMultiByte(CP_UTF8, 0, aText.data(), static_cast<int32_t>(aText.size()), nullptr, 0, NULL, NULL);
    if (len)
    {
        result.resize(len);
        len = WideCharToMultiByte(CP_UTF8, 0, aText.data(), static_cast<int32_t>(aText.size()), result.data(),
                                  static_cast<int32_t>(result.size()), NULL, NULL);
    }

    // Second pass.
    if (len <= 0)
    {
        result = fmt::format("Failed to convert wide to narrow string, last error is {}", Platform::GetLastError());
    }
#endif

    return result;
}

std::wstring Utils::Widen(const std::string_view aText)
{
    if (aText.empty())
    {
        return L"";
    }

    std::wstring result;

#ifdef RED4EXT_PLATFORM_MACOS
    const unsigned char* ptr = reinterpret_cast<const unsigned char*>(aText.data());
    const unsigned char* end = ptr + aText.size();

    while (ptr < end)
    {
        std::uint32_t cp = 0;
        const unsigned char c0 = *ptr++;

        if ((c0 & 0x80) == 0)
        {
            cp = c0;
        }
        else if ((c0 & 0xE0) == 0xC0 && ptr < end)
        {
            cp = static_cast<std::uint32_t>(c0 & 0x1F) << 6;
            cp |= static_cast<std::uint32_t>(*ptr++ & 0x3F);
        }
        else if ((c0 & 0xF0) == 0xE0 && (end - ptr) >= 2)
        {
            cp = static_cast<std::uint32_t>(c0 & 0x0F) << 12;
            cp |= static_cast<std::uint32_t>(*ptr++ & 0x3F) << 6;
            cp |= static_cast<std::uint32_t>(*ptr++ & 0x3F);
        }
        else if ((c0 & 0xF8) == 0xF0 && (end - ptr) >= 3)
        {
            cp = static_cast<std::uint32_t>(c0 & 0x07) << 18;
            cp |= static_cast<std::uint32_t>(*ptr++ & 0x3F) << 12;
            cp |= static_cast<std::uint32_t>(*ptr++ & 0x3F) << 6;
            cp |= static_cast<std::uint32_t>(*ptr++ & 0x3F);
        }
        else
        {
            cp = static_cast<std::uint32_t>('?');
        }

        result.push_back(static_cast<wchar_t>(cp));
    }
#else
    auto len = MultiByteToWideChar(CP_UTF8, 0, aText.data(), static_cast<int32_t>(aText.size()), nullptr, 0);
    if (len)
    {
        result.resize(len);
        len = MultiByteToWideChar(CP_UTF8, 0, aText.data(), static_cast<int32_t>(aText.size()), result.data(),
                                  static_cast<int32_t>(result.size()));
    }

    // Second pass.
    if (len <= 0)
    {
        result = fmt::format(L"Failed to convert narrow to wide string, last error is {}", Platform::GetLastError());
    }
#endif

    return result;
}

std::wstring Utils::ToLower(const std::wstring& acText)
{
    std::wstring text = acText;

    std::transform(text.begin(), text.end(), text.begin(), std::towlower);
    return text;
}
