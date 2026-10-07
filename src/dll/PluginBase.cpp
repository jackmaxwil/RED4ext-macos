#include "PluginBase.hpp"
#include "Platform.hpp"
#include "Utils.hpp"
#include "stdafx.hpp"
#ifdef RED4EXT_PLATFORM_MACOS
#include <dlfcn.h>
#endif

PluginBase::PluginBase(const std::filesystem::path& aPath, wil::unique_hmodule aModule)
    : m_path(aPath)
    , m_module(std::move(aModule))
{
}

const std::filesystem::path& PluginBase::GetPath() const
{
    return m_path;
}

Platform::Handle PluginBase::GetModule() const
{
    return m_module.get();
}

bool PluginBase::Query()
{
    const auto& path = GetPath();
    const auto stem = path.stem();
    const auto module = GetModule();

    Log::trace(L"Calling 'Query' function exported by '{}'...", stem);

    using Query_t = void (*)(void*);
#ifdef RED4EXT_PLATFORM_MACOS
    auto queryFn = reinterpret_cast<Query_t>(Platform::GetProcAddress(module, "Query"));
    if (!queryFn)
    {
        const char* err = dlerror();
        Log::warn(L"Could not retrieve 'Query' function from '{}'. Error: '{}', path: '{}'", stem,
                  err ? Utils::Widen(err) : L"Unknown error", path);
        return false;
    }
#else
    auto queryFn = reinterpret_cast<Query_t>(GetProcAddress(module, "Query"));
    if (!queryFn)
    {
        auto msg = Utils::FormatLastError();
        Log::warn(L"Could not retrieve 'Query' function from '{}'. Error code: {}, msg: '{}', path: '{}'", stem,
                  GetLastError(), msg, path);
        return false;
    }
#endif

    try
    {
        queryFn(GetPluginInfo());
    }
    catch (const std::exception& e)
    {
        Log::warn(L"An exception occured while calling 'Query' function exported by '{}'. Path: '{}'", stem, path);
        Log::warn(e.what());
        return false;
    }
    catch (...)
    {
        Log::warn(L"An unknown exception occured while calling 'Query' function exported by '{}'. Path: '{}'", stem,
                  path);
        return false;
    }

    auto name = GetName();
    if (name.empty())
    {
        Log::warn(L"'{}' does not have a name; one is required. Path: '{}'", stem, path);
        return false;
    }

    auto author = GetAuthor();
    if (author.empty())
    {
        Log::warn(L"'{}' does not have any author(s); an author is required. Path: '{}'", stem, path);
        return false;
    }

    Log::trace(L"'Query' function called successfully");
    return true;
}

bool PluginBase::Main(RED4ext::EMainReason aReason)
{
    const auto module = GetModule();
    const auto name = GetName();
    const auto reasonStr = aReason == RED4ext::EMainReason::Load ? L"Load" : L"Unload";

    Log::trace(L"Calling 'Main' function exported by '{}' with reason '{}'...", name, reasonStr);

    using Main_t = bool (*)(RED4ext::PluginHandle, RED4ext::EMainReason, const void*);
#ifdef RED4EXT_PLATFORM_MACOS
    auto mainFn = reinterpret_cast<Main_t>(Platform::GetProcAddress(module, "Main"));
#else
    auto mainFn = reinterpret_cast<Main_t>(GetProcAddress(module, "Main"));
#endif
    if (mainFn)
    {
        try
        {
            auto success = mainFn(module, aReason, GetSdkStruct());
            if (!success)
            {
                Log::trace(L"'Main' function returned 'false'");
                return false;
            }

            Log::trace(L"'Main' function called successfully");
        }
        catch (const std::exception& e)
        {
            Log::warn(L"An exception occured while calling 'Main' function with reason '{}', exported by '{}'",
                      reasonStr, name);
            Log::warn(e.what());
            return false;
        }
        catch (...)
        {
            Log::warn(L"An unknown exception occured while calling 'Main' function with reason '{}', exported by '{}'",
                      reasonStr, name);
            return false;
        }
    }
    else
    {
        Log::trace(L"'{}' does not export a 'Main' function, skipping the call", name);
    }

    return true;
}
