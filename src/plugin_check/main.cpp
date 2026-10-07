// red4ext_plugin_check DB PLUGIN.dylib...
// Applies the loader's plugin gate before launch: a plugin passes only if every address-DB hash compiled into it is
// verified with a non-zero offset. Launchers use it to compile only the scripts of plugins RED4ext will load (a
// refused plugin's script natives would stop the game with "Failed to initialize scripts data!").
// It also refuses a plugin that links a library missing on this Mac (e.g. a Homebrew library), since dlopen would fail.
// Prints "OK <path>" or "REFUSE <path> <reason>" per plugin; exit 0 if all pass, 1 if any is refused, 2 on usage error.
#include "../dll/Platform/PluginRequirements.hpp"

#include <simdjson.h>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <unordered_set>
#include <vector>

#include <mach-o/loader.h>

namespace
{
// The libraries a thin 64-bit Mach-O links (LC_LOAD_DYLIB; weak links may be missing) that are not on this Mac.
// System libraries live in the dyld shared cache, not on disk, so /usr/lib and /System paths count as present.
std::vector<std::string> MissingLibraries(const std::filesystem::path& aDylib)
{
    std::vector<std::string> missing;
    std::ifstream file(aDylib, std::ios::binary);
    std::vector<char> data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    if (data.size() < sizeof(mach_header_64))
        return missing;
    mach_header_64 header;
    std::memcpy(&header, data.data(), sizeof(header));
    if (header.magic != MH_MAGIC_64)
        return missing;

    size_t offset = sizeof(mach_header_64);
    for (uint32_t i = 0; i < header.ncmds && offset + sizeof(load_command) <= data.size(); ++i)
    {
        load_command command;
        std::memcpy(&command, data.data() + offset, sizeof(command));
        if (command.cmd == LC_LOAD_DYLIB && offset + sizeof(dylib_command) <= data.size())
        {
            dylib_command dylib;
            std::memcpy(&dylib, data.data() + offset, sizeof(dylib));
            const char* name = data.data() + offset + dylib.dylib.name.offset;
            std::string path(name, strnlen(name, command.cmdsize - dylib.dylib.name.offset));
            bool present = path.starts_with("/usr/lib/") || path.starts_with("/System/");
            if (!present && path.starts_with("@"))
            {
                // @rpath/@loader_path libraries are expected next to the plugin.
                present = std::filesystem::exists(aDylib.parent_path() / std::filesystem::path(path).filename());
            }
            else if (!present)
            {
                present = std::filesystem::exists(path);
            }
            if (!present)
                missing.push_back(path);
        }
        offset += command.cmdsize;
    }
    return missing;
}
} // namespace

int main(int argc, char** argv)
{
    if (argc < 3)
    {
        std::fprintf(stderr, "usage: %s cyberpunk2077_addresses.json PLUGIN.dylib...\n", argv[0]);
        return 2;
    }

    std::unordered_set<std::uint32_t> all;
    std::unordered_set<std::uint32_t> resolvable;
    {
        simdjson::ondemand::parser parser;
        simdjson::padded_string json;
        if (simdjson::padded_string::load(argv[1]).get(json))
        {
            std::fprintf(stderr, "cannot read %s\n", argv[1]);
            return 2;
        }
        simdjson::ondemand::document doc;
        simdjson::ondemand::array entries;
        if (parser.iterate(json).get(doc) || doc["Addresses"].get_array().get(entries))
        {
            std::fprintf(stderr, "%s: no Addresses array\n", argv[1]);
            return 2;
        }
        for (auto value : entries)
        {
            simdjson::ondemand::object entry;
            std::string_view hashStr;
            std::string_view offsetStr;
            if (value.get_object().get(entry) || entry["hash"].get_string().get(hashStr) ||
                entry["offset"].get_string().get(offsetStr))
            {
                std::fprintf(stderr, "%s: malformed entry\n", argv[1]);
                return 2;
            }
            bool verified = false;
            if (auto field = entry.find_field_unordered("verified"); !field.error())
            {
                field.get_bool().get(verified);
            }
            const auto hash = static_cast<std::uint32_t>(std::stoul(std::string(hashStr)));
            const auto colon = offsetStr.find(':');
            const auto offset = colon == std::string_view::npos
                                    ? 0
                                    : std::stoull(std::string(offsetStr.substr(colon + 1)), nullptr, 16);
            all.insert(hash);
            if (verified && offset != 0)
            {
                resolvable.insert(hash);
            }
        }
    }

    int status = 0;
    for (int i = 2; i < argc; ++i)
    {
        const auto constants = PluginRequirements::CollectConstants(argv[i]);
        if (!constants)
        {
            std::printf("REFUSE %s not a thin 64-bit Mach-O\n", argv[i]);
            status = 1;
            continue;
        }
        std::size_t missing = 0;
        for (const auto value : *constants)
        {
            missing += all.count(value) && !resolvable.count(value);
        }
        if (missing)
        {
            std::printf("REFUSE %s %zu unverified addresses\n", argv[i], missing);
            status = 1;
        }
        else
        {
            if (const auto libs = MissingLibraries(argv[i]); !libs.empty())
            {
                std::printf("REFUSE %s needs a library that is not on this Mac: %s\n", argv[i], libs.front().c_str());
                status = 1;
                continue;
            }
            std::printf("OK %s\n", argv[i]);
        }
    }
    return status;
}
