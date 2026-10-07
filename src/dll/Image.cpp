#include "Image.hpp"
#include "Platform.hpp"
#include "Utils.hpp"

#include <RED4ext/Api/v1/FileVer.hpp>
#include <RED4ext/Api/v1/SemVer.hpp>
#include <fmt/xchar.h>

#ifdef RED4EXT_PLATFORM_MACOS
#include <CoreFoundation/CoreFoundation.h>
#else
#include <wil/win32_helpers.h>

#include <Windows.h>
#endif

#include <cstdint>
#include <memory>
#include <new>
#include <string>
#include <string_view>
#include <vector>

Image::Image()
    : m_isCyberpunk(false)
    , m_fileVersion(RED4EXT_V1_FILEVER(0, 0, 0, 0))
    , m_productVersion(RED4EXT_V1_SEMVER(0, 0, 0))
{
#ifdef RED4EXT_PLATFORM_MACOS
    CFBundleRef bundle = CFBundleGetMainBundle();
    if (!bundle)
    {
        return;
    }

    CFDictionaryRef infoDict = CFBundleGetInfoDictionary(bundle);
    if (!infoDict)
    {
        return;
    }

    CFStringRef bundleId = (CFStringRef)CFDictionaryGetValue(infoDict, kCFBundleIdentifierKey);
    if (bundleId)
    {
        if (CFStringFind(bundleId, CFSTR("cdprojektred.cyberpunk"), kCFCompareCaseInsensitive).location != kCFNotFound)
        {
            m_isCyberpunk = true;
        }
    }

    if (m_isCyberpunk)
    {
        CFStringRef versionStr = (CFStringRef)CFDictionaryGetValue(infoDict, CFSTR("CFBundleShortVersionString"));
        if (versionStr)
        {
            char buffer[64];
            if (CFStringGetCString(versionStr, buffer, sizeof(buffer), kCFStringEncodingUTF8))
            {
                int major = 0, minor = 0, patch = 0;
                if (sscanf(buffer, "%d.%d.%d", &major, &minor, &patch) >= 1)
                {
                    m_fileVersion = RED4EXT_V1_FILEVER(major, minor, patch, 0);
                    m_productVersion = RED4EXT_V1_SEMVER(major, minor, patch);
                }
            }
        }
    }
#else
    auto fileName = Platform::GetModuleFileName();
    if (fileName.empty())
    {
        SHOW_LAST_ERROR_MESSAGE_FILE_LINE(L"Could not get executable's file name.");
        return;
    }

    auto size = GetFileVersionInfoSize(fileName.c_str(), nullptr);
    if (!size)
    {
        auto lastError = Platform::GetLastError();
        if (lastError == ERROR_RESOURCE_DATA_NOT_FOUND || lastError == ERROR_RESOURCE_TYPE_NOT_FOUND)
        {
            // Fail silently, executables might not have the version information.
            return;
        }

        SHOW_LAST_ERROR_MESSAGE_FILE_LINE(L"Could not retrieve version info size.\n\nFile name: {}", fileName);
    }

    std::unique_ptr<uint8_t[]> data(new (std::nothrow) uint8_t[size]());
    if (!data)
    {
        SHOW_MESSAGE_BOX_FILE_LINE(MB_ICONERROR | MB_OK, L"Could not allocate {} byte(s).\n\nFile name: {}", size,
                                   fileName);
        return;
    }

    if (!GetFileVersionInfo(fileName.c_str(), 0, size, data.get()))
    {
        SHOW_LAST_ERROR_MESSAGE_FILE_LINE(L"Could not retrieve version info.\n\nFile name: {}", fileName);
        return;
    }

    struct LangAndCodePage
    {
        WORD language;
        WORD codePage;
    }* translations;
    uint32_t translationsBytes;

    if (!VerQueryValue(data.get(), L"\\VarFileInfo\\Translation", reinterpret_cast<void**>(&translations),
                       &translationsBytes))
    {
        SHOW_LAST_ERROR_MESSAGE_FILE_LINE(L"Could not retrieve translations.\n\nFile name: {}", fileName);
        return;
    }

    for (uint32_t i = 0; i < (translationsBytes / sizeof(LangAndCodePage)); i++)
    {
        wchar_t* productName;
        auto subBlock = fmt::format(L"\\StringFileInfo\\{:04x}{:04x}\\ProductName", translations[i].language,
                                    translations[i].codePage);

        if (VerQueryValue(data.get(), subBlock.c_str(), reinterpret_cast<void**>(&productName), &translationsBytes))
        {
            constexpr std::wstring_view expectedProductName = L"Cyberpunk 2077";
            if (productName == expectedProductName)
            {
                m_isCyberpunk = true;
                break;
            }
        }
    }

    if (m_isCyberpunk)
    {
        VS_FIXEDFILEINFO* fileInfo = nullptr;
        UINT fileInfoBytes;

        if (!VerQueryValue(data.get(), L"\\", reinterpret_cast<LPVOID*>(&fileInfo), &fileInfoBytes))
        {
            SHOW_LAST_ERROR_MESSAGE_FILE_LINE(L"Could not retrieve the file's info.\n\nFile name: {}", fileName);
            return;
        }

        constexpr auto signature = 0xFEEF04BD;
        if (fileInfo->dwSignature != signature)
        {
            SHOW_LAST_ERROR_MESSAGE_FILE_LINE(
                L"The file's signature ({:#x}) did not match the expected value ({:#x}).\n\nFile name: {}",
                fileInfo->dwSignature, signature, fileName);
            return;
        }

        {
            uint16_t major = (fileInfo->dwFileVersionMS >> 16) & 0xFF;
            uint16_t minor = fileInfo->dwFileVersionMS & 0xFFFF;
            uint16_t build = (fileInfo->dwFileVersionLS >> 16) & 0xFFFF;
            uint16_t revision = fileInfo->dwFileVersionLS & 0xFFFF;

            m_fileVersion = RED4EXT_V1_FILEVER(major, minor, build, revision);
        }

        {
            uint8_t major = (fileInfo->dwProductVersionMS >> 16) & 0xFF;
            uint16_t minor = fileInfo->dwProductVersionMS & 0xFFFF;
            uint32_t patch = (fileInfo->dwProductVersionLS >> 16) & 0xFFFF;

            m_productVersion = RED4EXT_V1_SEMVER(major, minor, patch);
        }
    }
#endif
}

Image* Image::Get()
{
    static Image instance;
    return &instance;
}

bool Image::IsCyberpunk() const
{
    return m_isCyberpunk;
}

bool Image::IsSupported() const
{
    const auto supportedVersions = GetSupportedVersions();
    for (const auto& version : supportedVersions)
    {
        if (version == m_fileVersion)
        {
            return true;
        }
    }

    return false;
}

const RED4ext::v1::FileVer& Image::GetFileVersion() const
{
    return m_fileVersion;
}

const RED4ext::v1::SemVer& Image::GetProductVersion() const
{
    return m_productVersion;
}

const std::vector<RED4ext::v1::FileVer> Image::GetSupportedVersions() const
{
    return {m_fileVersion};
}
