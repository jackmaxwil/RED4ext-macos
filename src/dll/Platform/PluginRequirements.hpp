#pragma once

#ifdef RED4EXT_PLATFORM_MACOS
#include <cstdint>
#include <filesystem>
#include <optional>
#include <unordered_set>

namespace PluginRequirements
{
// 32-bit constants a plugin dylib can materialize (MOVZ/MOVK pairs, MOVN) or holds in its constant data. Address-DB
// hashes among them are the game addresses the plugin may resolve. Returns nullopt if the file is not a thin 64-bit
// Mach-O that can be parsed safely.
std::optional<std::unordered_set<std::uint32_t>> CollectConstants(const std::filesystem::path& aPath);
} // namespace PluginRequirements
#endif
