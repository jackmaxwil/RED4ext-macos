// red4ext_plugin_check DB PLUGIN.dylib...
// Applies the loader's plugin gate before launch: a plugin passes only if every address-DB hash compiled into it is
// verified with a non-zero offset. Launchers use it to compile only the scripts of plugins RED4ext will load (a
// refused plugin's script natives would stop the game with "Failed to initialize scripts data!").
// Prints "OK <path>" or "REFUSE <path> <reason>" per plugin; exit 0 if all pass, 1 if any is refused, 2 on usage error.
#include "../dll/Platform/PluginRequirements.hpp"

#include <simdjson.h>

#include <cstdio>
#include <string>
#include <unordered_set>

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
            std::printf("OK %s\n", argv[i]);
        }
    }
    return status;
}
