#include "ExecuteProcess.hpp"
#include "Addresses.hpp"
#include "App.hpp"
#include "Detail/AddressHashes.hpp"
#include "Hook.hpp"
#include "Platform.hpp"
#include "ScriptCompiler/ScriptCompilerSettings.hpp"
#include "Systems/ScriptCompilationSystem.hpp"
#ifndef RED4EXT_PLATFORM_MACOS
#include <windows.h>
#endif
#ifdef RED4EXT_PLATFORM_MACOS
#include <dlfcn.h>
#endif

namespace
{
bool isAttached = false;

bool _Global_ExecuteProcess(void* a1, RED4ext::CString& aCommand, FixedWString& aArgs,
                            RED4ext::CString& aCurrentDirectory, char a5);
Hook<decltype(&_Global_ExecuteProcess)> Global_ExecuteProcess(Hashes::Global_ExecuteProcess, &_Global_ExecuteProcess);

bool _Global_ExecuteProcess(void* a1, RED4ext::CString& aCommand, FixedWString& aArgs,
                            RED4ext::CString& aCurrentDirectory, char a5)
{
#ifdef RED4EXT_PLATFORM_MACOS
    // On macOS, the script compiler is likely "scc" instead of "scc.exe"
    const char* sccExeName = "scc.exe";
    const char* sccName = "scc";
    bool isScc = (strstr(aCommand.c_str(), sccExeName) != nullptr) || (strstr(aCommand.c_str(), sccName) != nullptr);
#else
    bool isScc = (strstr(aCommand.c_str(), "scc.exe") != nullptr);
#endif

    if (!isScc)
    {
        return Global_ExecuteProcess(a1, aCommand, aArgs, aCurrentDirectory, a5);
    }

    auto sccPath = std::filesystem::path(aCommand.c_str());
#ifdef RED4EXT_PLATFORM_MACOS
    // On macOS, scc_load_api is not available yet, fall back to CLI
    auto sccLib = sccPath.replace_filename("scc_lib.dylib");
    Log::info("Scc library loading not yet implemented on macOS ({}), falling back to CLI", sccLib.string());
#else
    auto& sccLib = sccPath.replace_filename("scc_lib.dll");
    auto sccHandle = LoadLibrary(sccLib.c_str());
    if (sccHandle)
    {
        auto scc = scc_load_api(sccHandle);
        return ExecuteScc(scc);
    }

    Log::info("Could not load the scc library from '{}', falling back to the CLI", sccLib.string());
#endif

    auto str = App::Get()->GetScriptCompilationSystem()->GetCompilationArgs(aArgs);

    FixedWString newArgs{};
    newArgs.str = str.c_str();
    newArgs.length = static_cast<std::uint32_t>(str.length());
    newArgs.maxLength = static_cast<std::uint32_t>(str.capacity());
#ifdef RED4EXT_PLATFORM_MACOS
    Log::info("Final redscript compilation arg string: '{}'", Log::Narrow(newArgs.str));
#else
    Log::info(L"Final redscript compilation arg string: '{}'", newArgs.str);
#endif
    return Global_ExecuteProcess(a1, aCommand, newArgs, aCurrentDirectory, a5);
}
} // namespace

bool Hooks::ExecuteProcess::Attach()
{
    Log::trace("Trying to attach the hook for execute process at {:#x}...", Global_ExecuteProcess.GetAddress());

    auto result = Global_ExecuteProcess.Attach();
    if (result != NO_ERROR)
    {
        Log::error("Could not attach the hook for execute process. Detour error code: {}", result);
    }
    else
    {
        Log::trace("The hook for execute process was attached");
    }

    isAttached = result == NO_ERROR;
    return isAttached;
}

bool Hooks::ExecuteProcess::Detach()
{
    if (!isAttached)
    {
        return false;
    }

    Log::trace("Trying to detach the hook for execute process at {:#x}...", Global_ExecuteProcess.GetAddress());

    auto result = Global_ExecuteProcess.Detach();
    if (result != NO_ERROR)
    {
        Log::error("Could not detach the hook for execute process. Detour error code: {}", result);
    }
    else
    {
        Log::trace("The hook for execute process was detached");
    }

    isAttached = result != NO_ERROR;
    return !isAttached;
}

bool ExecuteScc(SccApi& scc)
{
    auto scriptSystem = App::Get()->GetScriptCompilationSystem();
    auto engine = RED4ext::CGameEngine::Get();

    ScriptCompilerSettings settings(scc, App::Get()->GetPaths()->GetR6Dir());

    const auto blobPath = scriptSystem->HasScriptsBlob() ? scriptSystem->GetScriptsBlob()
                                                         : App::Get()->GetPaths()->GetDefaultScriptsBlob();

    auto moddedCacheFile = blobPath;
    moddedCacheFile.replace_extension("redscripts.modded");

    if (scriptSystem->HasScriptsBlob())
    {
        settings.SetCustomCacheFile(blobPath);
    }

    if (settings.SupportsOutputCacheFileParameter())
    {
        settings.SetOutputCacheFile(moddedCacheFile);
    }

    for (const auto& [_, path] : scriptSystem->GetScriptPaths())
    {
        settings.AddScriptPath(path);
    }

    for (const auto& type : scriptSystem->GetNeverRefTypes())
    {
        settings.RegisterNeverRefType(type);
    }

    for (const auto& type : scriptSystem->GetMixedRefTypes())
    {
        settings.RegisterMixedRefType(type);
    }

    const auto result = settings.Compile();

    if (const auto error = std::get_if<ScriptCompilerFailure>(&result))
    {
        engine->scriptsCompilationErrors = error->GetMessage().c_str();
        Log::warn("scc invocation failed with an error: {}", error->GetMessage());
        return false;
    }

    auto& sourceRepo = scriptSystem->GetSourceRefRepository();

    const auto& output = std::get<ScriptCompilerOutput>(result);
    const size_t refCount = output.GetSourceRefCount();

    for (size_t i = 0; i < refCount; ++i)
    {
        const auto sccRef = output.GetSourceRef(i);
        if (!sccRef.IsNative())
        {
            continue;
        }

        const SourceRef sourceRef{.file = sourceRepo.RegisterSourceFile(sccRef.GetPath()), .line = sccRef.GetLine()};

        switch (sccRef.GetType())
        {
        case SCC_SOURCE_REF_TYPE_CLASS:
            sourceRepo.RegisterClass(sccRef.GetName(), sourceRef);
            break;
        case SCC_SOURCE_REF_TYPE_FIELD:
            sourceRepo.RegisterProperty(sccRef.GetName(), sccRef.GetParentName(), sourceRef);
            break;
        case SCC_SOURCE_REF_TYPE_FUNCTION:
            auto parentName = sccRef.GetParentName();
            if (parentName.empty())
            {
                sourceRepo.RegisterFunction(sccRef.GetName(), sourceRef);
            }
            else
            {
                sourceRepo.RegisterMethod(sccRef.GetName(), parentName, sourceRef);
            }
            break;
        }
    }

    Log::info("scc invoked successfully, {} source refs were registered", refCount);

    if (settings.SupportsOutputCacheFileParameter())
    {
        scriptSystem->SetModdedScriptsBlob(moddedCacheFile);
#ifdef RED4EXT_PLATFORM_MACOS
        engine->scriptsBlobPath = moddedCacheFile.string().c_str();
        Log::info("Scripts blob path was updated to '{}'", moddedCacheFile.string());
#else
        engine->scriptsBlobPath = Utils::Narrow(moddedCacheFile.c_str());
        Log::info(L"Scripts blob path was updated to '{}'", moddedCacheFile);
#endif
    }

    return true;
}
