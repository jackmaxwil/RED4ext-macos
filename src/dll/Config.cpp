#include "Config.hpp"
#include "Paths.hpp"
#include "Utils.hpp"
#include "stdafx.hpp"

#include <spdlog/common.h>
#include <toml.hpp>

#ifndef RED4EXT_PLATFORM_MACOS
#include <Windows.h>
#endif

#include <exception>
#include <filesystem>
#include <iostream>
#include <string>
#include <system_error>
#include <vector>

#define DEFAULT_TOML_EXCEPTION_MSG L"An exception occured while parsing the config file:\n\n{}\n\nFile: {}"

Config::Config(const Paths& aPaths)
    : m_version(0)
    , m_dev()
    , m_logging()
    , m_plugins()
{
    const auto file = aPaths.GetConfigFile();

    std::error_code err;
    if (std::filesystem::exists(file, err))
    {
        Load(file);
    }
    else if (err)
    {
        auto errVal = err.value();
        const auto& category = err.category();
        auto msg = category.message(errVal);

        SHOW_MESSAGE_BOX_AND_EXIT_FILE_LINE(L"An error occured while checking config file existence:\n{}\n\nFile: {}",
                                            Utils::Widen(msg));
    }
    else
    {
        Save(file);
    }
}

const size_t Config::GetVersion() const
{
    return m_version;
}

const Config::DevConfig& Config::GetDev() const
{
    return m_dev;
}

const Config::LoggingConfig& Config::GetLogging() const
{
    return m_logging;
}

const Config::PluginsConfig& Config::GetPlugins() const
{
    return m_plugins;
}

const Config::HookingConfig& Config::GetHooking() const
{
    return m_hooking;
}

void Config::Load(const std::filesystem::path& aFile)
{
    try
    {
        auto config = toml::parse(aFile);
        if (config.contains("version"))
        {
            auto version = toml::find<size_t>(config, "version");
            switch (version)
            {
            case 0:
            {
                LoadV0(config);
                break;
            }
            default:
            {
                SHOW_MESSAGE_BOX_AND_EXIT_FILE_LINE(
                    "Config version '{}' is not supported (version < {} or version > {}).\n\nFile: {}", version,
                    MinSupportedVersion, MaxSupportedVersion, aFile);
                break;
            }
            }
        }
        else
        {
            SHOW_MESSAGE_BOX_AND_EXIT_FILE_LINE("The config file does not have a version.\n\nFile: {}", aFile);
        }
    }
    catch (const std::exception& e)
    {
        SHOW_MESSAGE_BOX_AND_EXIT_FILE_LINE(DEFAULT_TOML_EXCEPTION_MSG, Utils::Widen(e.what()), aFile);
    }
}

void Config::Save(const std::filesystem::path& aFile)
{
    try
    {
#ifdef RED4EXT_PLATFORM_MACOS
        using value_type = toml::basic_value<toml::preserve_comments, std::map>;
#else
        using value_type = toml::basic_value<toml::preserve_comments, tsl::ordered_map>;
#endif

        auto logLevel = spdlog::level::to_string_view(m_logging.level).data();
        auto flushOn = spdlog::level::to_string_view(m_logging.flushOn).data();

        value_type config{
            {"version", LatestVersion},
            {"logging", value_type{{"level", logLevel},
                                   {"flush_on", flushOn},
                                   {"max_files", m_logging.maxFiles},
                                   {"max_file_size", m_logging.maxFileSize}}},

            {"plugins", value_type{{"enabled", m_plugins.isEnabled}, {"ignored", std::vector<std::string>{}}}},
            {"hooking", value_type{{"core_hooks", std::vector<std::string>{}}}},
            {"dev", value_type{{"console", m_dev.hasConsole},
                               {"wait_for_debugger", m_dev.waitForDebugger},
                               {"strict_version_check", m_dev.strictVersionCheck}}}};

        config.comments().push_back(
            " See https://docs.red4ext.com/getting-started/configuration for more options or information.");

        std::ofstream file(aFile, std::ios::out);
        file.exceptions(std::ostream::badbit | std::ostream::failbit);

        file << config;
    }
    catch (const std::exception& e)
    {
        SHOW_MESSAGE_BOX_FILE_LINE(MB_ICONWARNING | MB_OK,
                                   "An exception occured while saving the config file:\n\n{}\n\nFile: {}",
                                   Utils::Widen(e.what()), aFile);
    }
}

void Config::LoadV0(const toml::value& aConfig)
{
    m_version = 0;

    m_dev.LoadV0(aConfig);
    m_logging.LoadV0(aConfig);
    m_plugins.LoadV0(aConfig);
    m_hooking.LoadV0(aConfig);

#ifdef RED4EXT_PLATFORM_MACOS
    bool strictSpecified = false;
    if (aConfig.contains("dev"))
    {
        const auto& dev = toml::find(aConfig, "dev");
        strictSpecified = dev.contains("strict_version_check");
    }

    if (!strictSpecified)
    {
        // Native code patching must only happen against the image the address DB was built for.
        m_dev.strictVersionCheck = true;
    }
#endif
}

void Config::DevConfig::LoadV0(const toml::value& aConfig)
{
    hasConsole = toml::find_or(aConfig, "dev", "console", hasConsole);
    waitForDebugger = toml::find_or(aConfig, "dev", "wait_for_debugger", waitForDebugger);
    strictVersionCheck = toml::find_or(aConfig, "dev", "strict_version_check", strictVersionCheck);
}

void Config::LoggingConfig::LoadV0(const toml::value& aConfig)
{
    auto levelName = toml::find_or(aConfig, "logging", "level", "");
    if (!levelName.empty())
    {
        auto requestedLevel = spdlog::level::from_str(levelName);

        // If the level is set to off, but the requested level is not "off" then the user might mistyped the levels.
        // spdlog return "level::off" if there is no match.
        if (requestedLevel == spdlog::level::off && levelName != "off")
        {
            requestedLevel = level;
        }

        level = requestedLevel;
    }

    levelName = toml::find_or(aConfig, "logging", "flush_on", "");
    if (!levelName.empty())
    {
        auto requestedLevel = spdlog::level::from_str(levelName.data());

        // Do not allow flushing to be off.
        if (requestedLevel == spdlog::level::off)
        {
            requestedLevel = flushOn;
        }

        flushOn = requestedLevel;
    }

    maxFiles = toml::find_or(aConfig, "logging", "max_files", maxFiles);
    if (maxFiles < 1)
    {
        maxFiles = 5;
    }

    maxFileSize = toml::find_or(aConfig, "logging", "max_file_size", maxFileSize);
    if (maxFileSize < 1)
    {
        maxFileSize = 10;
    }
}

void Config::PluginsConfig::LoadV0(const toml::value& aConfig)
{
    isEnabled = toml::find_or(aConfig, "plugins", "enabled", isEnabled);

    std::vector<std::string> ignoredPlugins;
    ignoredPlugins = toml::find_or(aConfig, "plugins", "ignored", ignoredPlugins);

    for (const auto& plugin : ignoredPlugins)
    {
#ifdef RED4EXT_PLATFORM_MACOS
        ignored.emplace(Utils::Widen(plugin));
#else
        ignored.emplace(Utils::Widen(plugin));
#endif
    }
}

void Config::HookingConfig::LoadV0(const toml::value& aConfig)
{
    coreHooks = toml::find_or(aConfig, "hooking", "core_hooks", std::vector<std::string>{});
}
