#pragma once

#include "Platform.hpp"

#include <RED4ext/Api/v1/EMainReason.hpp>
#include <RED4ext/Api/v1/FileVer.hpp>
#include <RED4ext/Api/v1/SemVer.hpp>

#ifndef RED4EXT_PLATFORM_MACOS
#include <wil/resource.h>

#include <Windows.h>
#endif

#include <cstdint>
#include <filesystem>
#include <string_view>

class PluginBase
{
public:
    PluginBase(const std::filesystem::path& aPath, wil::unique_hmodule aModule);
    virtual ~PluginBase() = default;

    virtual const uint32_t GetApiVersion() const = 0;
    virtual void* GetPluginInfo() = 0;
    virtual const void* GetSdkStruct() const = 0;

    virtual const std::wstring_view GetName() const = 0;
    virtual const std::wstring_view GetAuthor() const = 0;
    virtual const RED4ext::v1::SemVer& GetVersion() const = 0;
    virtual const RED4ext::v1::FileVer& GetRuntimeVersion() const = 0;
    virtual const RED4ext::v1::SemVer& GetSdkVersion() const = 0;

    const std::filesystem::path& GetPath() const;
    Platform::Handle GetModule() const;

    bool Query();
    bool Main(RED4ext::v1::EMainReason aReason);

private:
    std::filesystem::path m_path;
    wil::unique_hmodule m_module;
};
