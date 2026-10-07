#include "App.hpp"
#include "Addresses.hpp"
#include "Detail/AddressHashes.hpp"
#include "DetourTransaction.hpp"
#include "Image.hpp"
#include "Platform.hpp"
#include "Platform/Hooking.hpp"
#include "Utils.hpp"
#include "Version.hpp"

#ifdef RED4EXT_PLATFORM_MACOS
#include "Platform/NativeHook.hpp"
#endif

#include "Hooks/AssertionFailed.hpp"
#include "Hooks/CGameApplication.hpp"
#include "Hooks/CollectSaveableSystems.hpp"
#include "Hooks/ExecuteProcess.hpp"
#include "Hooks/InitScripts.hpp"
#include "Hooks/LoadScripts.hpp"
#include "Hooks/Main_Hooks.hpp"
#include "Hooks/ValidateScripts.hpp"
#include "Hooks/gsmState_SessionActive.hpp"

#include <cstdio>

namespace
{
std::unique_ptr<App> g_app;

#ifdef RED4EXT_PLATFORM_MACOS
__attribute__((noinline, optnone, aligned(16384), section("__TEXT,__nhself"))) static int NativeHookSelfTarget(int x)
{
    return x + 1;
}

static int NativeHookSelfDetour(int)
{
    return 42;
}

bool RunNativeHookSelfTest()
{
    if (NativeHookSelfTarget(1) != 2)
    {
        Log::error("Native hook self-test target returned an unexpected value before patching");
        return false;
    }

    auto* targetFn = &NativeHookSelfTarget;
    void* original = reinterpret_cast<void*>(targetFn);
    DetourTransaction transaction;
    if (!transaction.IsValid() || DetourAttach(&original, reinterpret_cast<void*>(&NativeHookSelfDetour)) != NO_ERROR ||
        !transaction.Commit())
    {
        Log::error("Native hook self-test could not install its detour");
        return false;
    }

    const int hooked = NativeHookSelfTarget(1);

    DetourTransaction detach;
    if (detach.IsValid())
    {
        DetourDetach(&original, reinterpret_cast<void*>(&NativeHookSelfDetour));
        detach.Commit();
    }

    if (hooked != 42 || NativeHookSelfTarget(1) != 2)
    {
        Log::error("Native hook self-test detour did not run or the target was not restored");
        return false;
    }

    Log::info("Native hook engine self-test passed");
    return true;
}
#endif

} // namespace

App::App()
    : m_config(m_paths)
    , m_devConsole(m_config.GetDev())
{
    if (m_config.GetDev().waitForDebugger)
    {
        while (!Platform::IsDebuggerPresent())
        {
            std::this_thread::yield();
        }
    }

    AddSystem<LoggerSystem>(m_paths, m_config, m_devConsole);
    AddSystem<ScriptCompilationSystem>(m_paths);
    AddSystem<HookingSystem>();
    AddSystem<StateSystem>();
    AddSystem<PluginSystem>(m_config.GetPlugins(), m_paths);

    m_systems.shrink_to_fit();

    const auto filename = fmt::format(L"red4ext-{}.log", Utils::FormatCurrentTimestamp());

    auto logger = Utils::CreateLogger(L"RED4ext", filename, m_paths, m_config, m_devConsole);
    spdlog::set_default_logger(logger);

    Log::info("RED4ext (v{}) is initializing...", RED4EXT_VERSION_STR);

    Log::debug("Using the following paths:");
    Log::debug(L"  Root: {}", m_paths.GetRootDir());
    Log::debug(L"  RED4ext: {}", m_paths.GetRED4extDir());
    Log::debug(L"  Logs: {}", m_paths.GetLogsDir());
    Log::debug(L"  Config: {}", m_paths.GetConfigFile());
    Log::debug(L"  Plugins: {}", m_paths.GetPluginsDir());

    Log::debug("Using the following configuration:");
    Log::debug("  version: {}", m_config.GetVersion());

    const auto& dev = m_config.GetDev();
    Log::debug("  dev.console: {}", dev.hasConsole);

    const auto& loggingConfig = m_config.GetLogging();
    Log::debug("  logging.level: {}", spdlog::level::to_string_view(loggingConfig.level));
    Log::debug("  logging.flush_on: {}", spdlog::level::to_string_view(loggingConfig.flushOn));
    Log::debug("  logging.max_files: {}", loggingConfig.maxFiles);
    Log::debug("  logging.max_file_size: {} MB", loggingConfig.maxFileSize);

    const auto& pluginsConfig = m_config.GetPlugins();
    Log::debug("  plugins.enabled: {}", pluginsConfig.isEnabled);

    const auto& ignored = pluginsConfig.ignored;
    if (ignored.empty())
    {
        Log::debug("  plugins.ignored: []");
    }
    else
    {
#ifdef RED4EXT_PLATFORM_MACOS
        // On macOS, convert wstrings to strings for logging
        std::vector<std::string> ignoredNarrow;
        for (const auto& ws : ignored)
        {
            ignoredNarrow.push_back(Log::Narrow(ws));
        }
        Log::debug("  plugins.ignored: [ {} ]", fmt::join(ignoredNarrow, ", "));
#else
        Log::debug(L"  plugins.ignored: [ {} ]", fmt::join(ignored, L", "));
#endif
    }

    Log::debug("Base address is: {}", reinterpret_cast<void*>(Platform::GetModuleHandle(nullptr)));

    const auto image = Image::Get();
    const auto& fileVer = image->GetFileVersion();

    const auto& productVer = image->GetProductVersion();
    Log::info("Product version: {}.{}{}", productVer.major, productVer.minor, productVer.patch);
    Log::info("File version: {}.{}.{}.{}", fileVer.major, fileVer.minor, fileVer.build, fileVer.revision);

#ifdef RED4EXT_PLATFORM_MACOS
    Log::info("macOS runtime version detected: {}.{}.{}.{}", fileVer.major, fileVer.minor, fileVer.build,
              fileVer.revision);
#else
    auto minimumVersion = RED4EXT_RUNTIME_2_31;
    if (fileVer < RED4EXT_RUNTIME_2_31)
    {
        Log::error(L"To use this version of RED4ext, ensure your game is updated to patch 2.31 or newer");
        return;
    }
#endif

    Addresses::Construct(m_paths);

#ifdef RED4EXT_PLATFORM_MACOS
    {
        char imageUuid[80]{};
        NativeHook::MainImageUuid(imageUuid, sizeof(imageUuid));
        char imageVersion[32];
        std::snprintf(imageVersion, sizeof(imageVersion), "%u.%u.%u", productVer.major, productVer.minor,
                      productVer.patch);
        const char* dbVersion = "";
        const char* dbUuid = "";
        if (auto* addresses = Addresses::Instance())
        {
            dbVersion = addresses->GetDatabaseGameVersion().c_str();
            dbUuid = addresses->GetDatabaseUuid().c_str();
        }

        char line[640];
        if (!NativeHook::AllowWrites(dbVersion, dbUuid, imageVersion, imageUuid, m_config.GetDev().strictVersionCheck,
                                     line, sizeof(line)))
        {
            Log::error("{}", line);
            return;
        }
    }
#endif

    if (AttachHooks())
    {
        Log::info("RED4ext has been successfully initialized");
    }
    else
    {
        Log::error("RED4ext did not initialize properly");
    }
}

void App::Construct()
{
    g_app.reset(new App());
}

void App::Destruct()
{
#ifdef RED4EXT_PLATFORM_MACOS
    NativeHook::StopStatsThread();
#endif

    Log::info("RED4ext is terminating...");

#ifdef RED4EXT_PLATFORM_MACOS
    // Destruct only runs at process exit on macOS (the dylib is never unloaded early). Restoring patched code then
    // gains nothing and races any thread still inside a hooked function, so the hooks stay installed.
#else
    // Detaching hooks here and not in dtor, since the dtor can be called by CRT when the processes exists. We don't
    // really care if this will be called or not when the game exist ungracefully.

    Log::trace("Detaching the hooks...");

    DetourTransaction transaction;
    if (transaction.IsValid())
    {
#ifdef RED4EXT_PLATFORM_MACOS
        auto success = Hooks::CGameApplication::Detach() && Hooks::Main::Detach() && Hooks::ExecuteProcess::Detach() &&
                       Hooks::InitScripts::Detach() && Hooks::LoadScripts::Detach() &&
                       Hooks::ValidateScripts::Detach() && Hooks::AssertionFailed::Detach() &&
                       Hooks::CollectSaveableSystems::Detach() && Hooks::gsmState_SessionActive::Detach();
#else
        auto success = Hooks::CGameApplication::Detach() && Hooks::Main::Detach() && Hooks::ExecuteProcess::Detach() &&
                       Hooks::InitScripts::Detach() && Hooks::LoadScripts::Detach() &&
                       Hooks::ValidateScripts::Detach() && Hooks::AssertionFailed::Detach() &&
                       Hooks::gsmState_SessionActive::Detach();
#endif
        if (success)
        {
            transaction.Commit();
        }
    }

#endif

    g_app.reset(nullptr);
    Log::info("RED4ext has been terminated");

    spdlog::details::registry::instance().flush_all();
    spdlog::shutdown();
}

App* App::Get()
{
    return g_app.get();
}

void App::Startup()
{
    Log::info("RED4ext is starting up...");

    for (auto& system : m_systems)
    {
        system->Startup();
    }

    auto pluginNames = GetPluginSystem()->GetActivePlugins();
    GetLoggerSystem()->RotateLogs(pluginNames);

    Log::info("RED4ext has been started");
}

namespace
{
std::atomic<bool> g_shuttingDown{false};
}

bool App::IsShuttingDown()
{
    return g_shuttingDown.load();
}

void App::Shutdown()
{
    g_shuttingDown.store(true);
    Log::info("RED4ext is shutting down...");

    // One failing system must not skip the others (or the logs and hook stats written after them).
    for (auto& system : m_systems | std::ranges::views::reverse)
    {
        try
        {
            system->Shutdown();
        }
        catch (const std::exception& e)
        {
            Log::error("System {} failed to shut down: {}", static_cast<int32_t>(system->GetType()), e.what());
        }
    }

    m_systems.clear();
    Log::info("RED4ext has been shut down");

    // Flushing the log here, since it is called in the main function, not when DLL is unloaded.
    spdlog::details::registry::instance().flush_all();
}

LoggerSystem* App::GetLoggerSystem()
{
    auto& system = m_systems.at(static_cast<size_t>(ESystemType::Logger));
    return static_cast<LoggerSystem*>(system.get());
}

HookingSystem* App::GetHookingSystem()
{
    auto& system = m_systems.at(static_cast<size_t>(ESystemType::Hooking));
    return static_cast<HookingSystem*>(system.get());
}

StateSystem* App::GetStateSystem()
{
    auto& system = m_systems.at(static_cast<size_t>(ESystemType::State));
    return static_cast<StateSystem*>(system.get());
}

PluginSystem* App::GetPluginSystem()
{
    auto& system = m_systems.at(static_cast<size_t>(ESystemType::Plugin));
    return static_cast<PluginSystem*>(system.get());
}

ScriptCompilationSystem* App::GetScriptCompilationSystem()
{
    auto& system = m_systems.at(static_cast<size_t>(ESystemType::Script));
    return static_cast<ScriptCompilationSystem*>(system.get());
}

const Paths* App::GetPaths() const
{
    return &m_paths;
}

bool App::AttachHooks() const
{
    Log::trace("Attaching hooks...");

#ifdef RED4EXT_PLATFORM_MACOS
    if (!RunNativeHookSelfTest())
    {
        Log::error("Native hook engine self-test failed; not installing game hooks");
        return false;
    }

    DetourTransaction transaction;
    if (!transaction.IsValid())
    {
        return false;
    }

    // On macOS, attach hooks individually and continue even if some fail.

    // A core hook is attempted only when its address is verified and, if the user set an allowlist, it is listed.
    const auto& allow = m_config.GetHooking().coreHooks;
    const auto addresses = Addresses::Instance();
    auto enabled = [&allow, addresses](const char* aName, std::uint32_t aHash)
    {
        if (!addresses || !addresses->UnresolvedAmong({aHash}).empty())
        {
            Log::debug("Skipping core hook {}: its address is not verified", aName);
            return false;
        }
        return allow.empty() || std::find(allow.begin(), allow.end(), aName) != allow.end();
    };

    int successCount = 0;
    int totalHooks = 1;

    // Always required: plugins are loaded from the game's main(), not from the dylib constructor.
    if (Hooks::Main::Attach())
        successCount++;
    else
        Log::error("main() hook failed - plugins will not be loaded");

    if (enabled("CGameApplication_AddState", Hashes::CGameApplication_AddState))
    {
        totalHooks++;
        if (Hooks::CGameApplication::Attach())
            successCount++;
        else
            Log::warn("CGameApplication hook failed - state management may be limited");
    }

    if (enabled("Global_ExecuteProcess", Hashes::Global_ExecuteProcess))
    {
        totalHooks++;
        if (Hooks::ExecuteProcess::Attach())
            successCount++;
        else
            Log::warn("ExecuteProcess hook failed - script compilation redirection unavailable");
    }

    if (enabled("CBaseEngine_InitScripts", Hashes::CBaseEngine_InitScripts))
    {
        totalHooks++;
        if (Hooks::InitScripts::Attach())
            successCount++;
        else
            Log::warn("InitScripts hook failed - script initialization hooks unavailable");
    }

    if (enabled("CBaseEngine_LoadScripts", Hashes::CBaseEngine_LoadScripts))
    {
        totalHooks++;
        if (Hooks::LoadScripts::Attach())
            successCount++;
        else
            Log::warn("LoadScripts hook failed - script loading hooks unavailable");
    }

    if (enabled("ScriptValidator_Validate", Hashes::ScriptValidator_Validate))
    {
        totalHooks++;
        if (Hooks::ValidateScripts::Attach())
            successCount++;
        else
            Log::warn("ValidateScripts hook failed - script validation hooks unavailable");
    }

    if (enabled("AssertionFailed", Hashes::AssertionFailed))
    {
        totalHooks++;
        if (Hooks::AssertionFailed::Attach())
            successCount++;
        else
            Log::warn("AssertionFailed hook failed - assertion logging unavailable");
    }

    if (enabled("GameInstance_CollectSaveableSystems", Hashes::GameInstance_CollectSaveableSystems))
    {
        totalHooks++;
        if (Hooks::CollectSaveableSystems::Attach())
            successCount++;
        else
            Log::warn("CollectSaveableSystems hook failed - save system hooks unavailable");
    }

    if (enabled("GsmState_SessionActive_ReportErrorCode", Hashes::GsmState_SessionActive_ReportErrorCode))
    {
        totalHooks++;
        if (Hooks::gsmState_SessionActive::Attach())
            successCount++;
        else
            Log::warn("gsmState_SessionActive hook failed - session state hooks unavailable");
    }

    Log::info("Attached {}/{} hooks successfully", successCount, totalHooks);

    const bool committed = transaction.Commit();
    if (committed)
    {
        char imageUuid[80]{};
        NativeHook::MainImageUuid(imageUuid, sizeof(imageUuid));
        const auto statsPath = m_paths.GetLogsDir() / "hookstats.json";
        NativeHook::StartStatsThread(statsPath.string().c_str(), imageUuid);
    }

    return committed;
#else
    DetourTransaction transaction;
    if (!transaction.IsValid())
    {
        return false;
    }

    auto success = Hooks::Main::Attach() && Hooks::CGameApplication::Attach() && Hooks::ExecuteProcess::Attach() &&
                   Hooks::InitScripts::Attach() && Hooks::LoadScripts::Attach() && Hooks::ValidateScripts::Attach() &&
                   Hooks::AssertionFailed::Attach() && Hooks::CollectSaveableSystems::Attach() &&
                   Hooks::gsmState_SessionActive::Attach();
    if (success)
    {
        return transaction.Commit();
    }

    return false;
#endif
}
