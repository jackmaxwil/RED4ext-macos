#include "App.hpp"
#include "Addresses.hpp"
#include "DetourTransaction.hpp"
#include "Image.hpp"
#include "Platform.hpp"
#include "Platform/Hooking.hpp"
#include "Platform/CrashHandler.hpp"
#include "Platform/RuntimeValidation.hpp"
#include "Platform/StructuredLogging.hpp"
#include "Platform/PluginMonitor.hpp"
#include "Utils.hpp"
#include "Version.hpp"

#ifdef RED4EXT_PLATFORM_MACOS
#include <libkern/OSCacheControl.h>
#include <cerrno>
#include <cstdlib>
#include "Detail/AddressHashes.hpp"
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

namespace
{
std::unique_ptr<App> g_app;

#ifdef RED4EXT_PLATFORM_MACOS
bool IsEnvFlagEnabled(const char* aName)
{
    const char* value = std::getenv(aName);
    return value && *value && !(value[0] == '0' && value[1] == '\0');
}

bool TestTextPatchFeasibility()
{
    constexpr uint32_t kPatchSize = 4;
    constexpr uint32_t kNop = 0xD503201F; // NOP

    auto* addresses = Addresses::Instance();
    if (!addresses)
    {
        Log::error("[HookingPOC] Addresses not initialized");
        return false;
    }

    auto target = reinterpret_cast<void*>(addresses->Resolve(Hashes::CGameApplication_AddState));
    if (!target)
    {
        Log::error("[HookingPOC] Could not resolve CGameApplication_AddState");
        return false;
    }

    uint32_t original = 0;
    std::memcpy(&original, target, sizeof(original));

    Log::info("[HookingPOC] Testing __TEXT patchability at CGameApplication_AddState={}, original={:#x}", target,
              original);

    uint32_t oldProt = 0;
    if (!Platform::ProtectMemory(target, kPatchSize, Platform::Memory_ExecuteReadWrite, &oldProt))
    {
        Log::error("[HookingPOC] ProtectMemory(RWX) failed: errno={} oldProt={:#x}", errno, oldProt);
        return false;
    }

    std::memcpy(target, &kNop, sizeof(kNop));
    sys_icache_invalidate(target, kPatchSize);

    uint32_t readback = 0;
    std::memcpy(&readback, target, sizeof(readback));

    // Restore
    std::memcpy(target, &original, sizeof(original));
    sys_icache_invalidate(target, kPatchSize);
    Platform::ProtectMemory(target, kPatchSize, oldProt, nullptr);

    if (readback != kNop)
    {
        Log::error("[HookingPOC] Write verification failed: wrote={:#x} readback={:#x}", kNop, readback);
        return false;
    }

    Log::info("[HookingPOC] __TEXT patch test succeeded (wrote NOP and restored)");
    return true;
}
#endif
}

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

#ifdef RED4EXT_PLATFORM_MACOS
    // Initialize structured logging early
    Platform::StructuredLogging::Initialize(m_paths.GetLogsDir().string());
    
    // Enable JSON export if requested
    const char* jsonLogEnv = std::getenv("RED4EXT_JSON_LOG");
    if (jsonLogEnv && strlen(jsonLogEnv) > 0)
    {
        std::string jsonLogPath = std::string(jsonLogEnv);
        Platform::StructuredLogging::SetJSONExport(true, jsonLogPath);
        Log::info("[StructuredLogging] JSON export enabled: {}", jsonLogPath);
    }
    
    // Initialize crash handlers early
    Platform::CrashHandler::Initialize();
    Log::info("[CrashHandler] Signal handlers installed (SIGSEGV, SIGBUS, SIGILL, SIGFPE)");
    Platform::CrashHandler::LogLoadedImages();

    // Perform initial health check
    Platform::StructuredLogging::PushContext("HealthCheck", "App", "", nullptr, 0);
    auto healthCheck = Platform::RuntimeValidation::PerformHealthCheck();
    if (!healthCheck.isHealthy)
    {
        Log::error("[RuntimeValidation] Health check FAILED - {} error(s) detected", healthCheck.errors.size());
        for (const auto& error : healthCheck.errors)
        {
            Log::error("[RuntimeValidation]   ERROR: {}", error);
        }
    }
    else
    {
        Log::info("[RuntimeValidation] Initial health check passed");
    }
    if (!healthCheck.warnings.empty())
    {
        Log::warn("[RuntimeValidation] Health check warnings: {}", healthCheck.warnings.size());
        for (const auto& warning : healthCheck.warnings)
        {
            Log::warn("[RuntimeValidation]   WARNING: {}", warning);
        }
    }
#endif

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
    // On macOS, version scheme differs from Windows (CFBundleShortVersionString vs PE version)
    // Skip version check for now - macOS port is tested with v2.3.1
    Log::info("macOS port - version check bypassed (game version: {}.{}.{}.{})", 
              fileVer.major, fileVer.minor, fileVer.build, fileVer.revision);
#else
    auto minimumVersion = RED4EXT_RUNTIME_2_31;
    if (fileVer < RED4EXT_RUNTIME_2_31)
    {
        Log::error(L"To use this version of RED4ext, ensure your game is updated to patch 2.31 or newer");
        return;
    }
#endif

    Addresses::Construct(m_paths);

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
    Log::info("RED4ext is terminating...");
#ifdef RED4EXT_PLATFORM_MACOS
    // Export log summary before shutdown
    if (auto* app = App::Get())
    {
        auto summaryPath = app->m_paths.GetLogsDir() / "red4ext_summary.json";
        Platform::StructuredLogging::ExportSummaryToJSON(summaryPath.string());
    }
    Platform::StructuredLogging::Shutdown();
#endif

    // Detaching hooks here and not in dtor, since the dtor can be called by CRT when the processes exists. We don't
    // really care if this will be called or not when the game exist ungracefully.

    Log::trace("Detaching the hooks...");

    DetourTransaction transaction;
    if (transaction.IsValid())
    {
#ifdef RED4EXT_PLATFORM_MACOS
        auto success = Hooks::CGameApplication::Detach() && Hooks::ExecuteProcess::Detach() &&
                       Hooks::InitScripts::Detach() && Hooks::LoadScripts::Detach() &&
                       Hooks::ValidateScripts::Detach() && Hooks::AssertionFailed::Detach() &&
                       Hooks::gsmState_SessionActive::Detach();
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

#ifdef RED4EXT_PLATFORM_MACOS
    // Generate plugin health report
    auto healthReport = Platform::PluginMonitor::GenerateHealthReport();
    Log::debug("[PluginMonitor] Plugin health status:\n{}", healthReport);
    
    auto problematicPlugins = Platform::PluginMonitor::GetProblematicPlugins();
    if (!problematicPlugins.empty())
    {
        Log::warn("[PluginMonitor] {} problematic plugin(s) detected:", problematicPlugins.size());
        for (const auto& name : problematicPlugins)
        {
            auto health = Platform::PluginMonitor::GetPluginHealth(name);
            Log::warn("[PluginMonitor]   - {}: {} error(s), {} hook failure(s)", 
                     name, health.errors.size(), health.hookFailures);
        }
    }
#endif

    Log::info("RED4ext has been started");
}

void App::Shutdown()
{
    Log::info("RED4ext is shutting down...");

    for (auto& system : m_systems | std::ranges::views::reverse)
    {
        system->Shutdown();
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
    Platform::StructuredLogging::LogSectionStart("HookAttachment");
    Platform::StructuredLogging::PushContext("AttachHooks", "App");
#endif

#ifdef RED4EXT_PLATFORM_MACOS
    if (IsEnvFlagEnabled("RED4EXT_DISABLE_ALL_HOOKS"))
    {
        Log::warn("[Hooking] All RED4ext hooks disabled via RED4EXT_DISABLE_ALL_HOOKS=1");
        return true;
    }

    DetourSetBackend(static_cast<int32_t>(m_config.GetHooking().backend));

    // Phase 0 gate: if native is requested, first verify we can patch __TEXT.
    if (m_config.GetHooking().backend == Config::HookingConfig::Backend::NativeInline)
    {
        {
            DetourTransaction transaction;
            if (!transaction.IsValid())
            {
                return false;
            }

            const bool canPatch = TestTextPatchFeasibility();
            transaction.Commit();

            if (!canPatch)
            {
                Log::warn("[HookingPOC] Native inline patching appears NOT viable; falling back to Frida backend");
                DetourSetBackend(static_cast<int32_t>(Config::HookingConfig::Backend::FridaGadget));
            }
            else
            {
                Log::info("[HookingPOC] Native inline patching appears viable; proceeding with native hooks");
            }
        }
    }

    DetourTransaction transaction;
    if (!transaction.IsValid())
    {
        return false;
    }

    // On macOS, attach hooks individually and continue even if some fail.
    
    int successCount = 0;
    int totalHooks = 0;

    auto tryAttach = [&](const char* envVar, const char* name, auto&& fnAttach, const char* failMsg) {
        if (IsEnvFlagEnabled(envVar))
        {
            Log::warn("[Hooking] {} hook disabled via {}=1", name, envVar);
            return;
        }

        totalHooks++;
        if (fnAttach())
        {
            successCount++;
        }
        else
        {
            Log::warn("{}", failMsg);
        }
    };

    tryAttach("RED4EXT_DISABLE_HOOK_CGAMEAPPLICATION",
              "CGameApplication",
              []() { return Hooks::CGameApplication::Attach(); },
              "CGameApplication hook failed - state management may be limited");

    tryAttach("RED4EXT_DISABLE_HOOK_EXECUTEPROCESS",
              "ExecuteProcess",
              []() { return Hooks::ExecuteProcess::Attach(); },
              "ExecuteProcess hook failed - script compilation redirection unavailable");

    tryAttach("RED4EXT_DISABLE_HOOK_INITSCRIPTS",
              "InitScripts",
              []() { return Hooks::InitScripts::Attach(); },
              "InitScripts hook failed - script initialization hooks unavailable");

    tryAttach("RED4EXT_DISABLE_HOOK_LOADSCRIPTS",
              "LoadScripts",
              []() { return Hooks::LoadScripts::Attach(); },
              "LoadScripts hook failed - script loading hooks unavailable");

    tryAttach("RED4EXT_DISABLE_HOOK_VALIDATESCRIPTS",
              "ValidateScripts",
              []() { return Hooks::ValidateScripts::Attach(); },
              "ValidateScripts hook failed - script validation hooks unavailable");

    tryAttach("RED4EXT_DISABLE_HOOK_ASSERTIONFAILED",
              "AssertionFailed",
              []() { return Hooks::AssertionFailed::Attach(); },
              "AssertionFailed hook failed - assertion logging unavailable");

    tryAttach("RED4EXT_DISABLE_HOOK_COLLECTSAVEABLESYSTEMS",
              "CollectSaveableSystems",
              []() { return Hooks::CollectSaveableSystems::Attach(); },
              "CollectSaveableSystems hook failed - save system hooks unavailable");

    tryAttach("RED4EXT_DISABLE_HOOK_SESSIONACTIVE",
              "gsmState_SessionActive",
              []() { return Hooks::gsmState_SessionActive::Attach(); },
              "gsmState_SessionActive hook failed - session state hooks unavailable");
    
    Log::info("Attached {}/{} hooks successfully", successCount, totalHooks);
    
#ifdef RED4EXT_PLATFORM_MACOS
    // Log hook statistics
    auto hookStats = Platform::RuntimeValidation::GetHookStatistics();
    if (hookStats.totalHooks > 0)
    {
        Log::info("[RuntimeValidation] Hook statistics: total={} success={} failed={} invalid_targets={}", 
                  hookStats.totalHooks, hookStats.successfulHooks, hookStats.failedHooks, hookStats.invalidTargets);
        
        if (!hookStats.failedHookDetails.empty())
        {
            Log::warn("[RuntimeValidation] Failed hook details (showing first {}):", 
                      std::min(static_cast<size_t>(5), hookStats.failedHookDetails.size()));
            for (size_t i = 0; i < std::min(static_cast<size_t>(5), hookStats.failedHookDetails.size()); ++i)
            {
                const auto& [target, reason] = hookStats.failedHookDetails[i];
                Log::warn("[RuntimeValidation]   {}: {}", fmt::ptr(target), reason);
            }
        }
    }

    // Post-hook health check
    auto postHookHealth = Platform::RuntimeValidation::PerformHealthCheck();
    if (!postHookHealth.isHealthy)
    {
        Log::error("[RuntimeValidation] Post-hook health check FAILED");
        for (const auto& error : postHookHealth.errors)
        {
            Log::error("[RuntimeValidation]   ERROR: {}", error);
        }
    }
    if (!postHookHealth.warnings.empty())
    {
        Log::warn("[RuntimeValidation] Post-hook warnings: {}", postHookHealth.warnings.size());
        for (const auto& warning : postHookHealth.warnings)
        {
            Log::warn("[RuntimeValidation]   WARNING: {}", warning);
        }
    }
#endif
    
    // On macOS, we consider initialization successful even with partial hooks
    // Plugin loading and basic functionality should still work
    transaction.Commit();
#ifdef RED4EXT_PLATFORM_MACOS
    Platform::StructuredLogging::PopContext();
    Platform::StructuredLogging::LogSectionEnd("HookAttachment");
#endif
    return true;
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
