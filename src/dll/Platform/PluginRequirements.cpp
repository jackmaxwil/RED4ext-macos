#include "stdafx.hpp"
#include "PluginRequirements.hpp"

#ifdef RED4EXT_PLATFORM_MACOS
#include <mach-o/loader.h>

#include <cstring>
#include <fstream>
#include <iterator>
#include <string_view>
#include <vector>

namespace
{
template<typename T>
bool Read(const std::vector<char>& aData, std::size_t aOffset, T& aOut)
{
    if (aOffset > aData.size() || aData.size() - aOffset < sizeof(T))
    {
        return false;
    }
    std::memcpy(&aOut, aData.data() + aOffset, sizeof(T));
    return true;
}

void ScanCode(const std::vector<char>& aData, std::size_t aOffset, std::size_t aSize,
              std::unordered_set<std::uint32_t>& aOut)
{
    std::uint32_t pending[32]{};
    bool hasPending[32]{};
    for (std::size_t i = 0; i + 4 <= aSize; i += 4)
    {
        std::uint32_t w;
        std::memcpy(&w, aData.data() + aOffset + i, sizeof(w));
        const auto rd = w & 31;
        const auto imm = (w >> 5) & 0xFFFF;
        const auto hw = (w >> 21) & 3;
        switch (w & 0x7F800000)
        {
        case 0x52800000: // MOVZ
            if (hw == 0)
            {
                pending[rd] = imm;
                hasPending[rd] = true;
                aOut.insert(imm);
            }
            break;
        case 0x72800000: // MOVK
            if (hw == 1 && hasPending[rd])
            {
                aOut.insert(pending[rd] | (imm << 16));
                hasPending[rd] = false;
            }
            break;
        case 0x12800000: // MOVN
            if (hw == 0)
            {
                aOut.insert(~imm);
            }
            break;
        default:
            break;
        }
    }
}

void ScanData(const std::vector<char>& aData, std::size_t aOffset, std::size_t aSize,
              std::unordered_set<std::uint32_t>& aOut)
{
    for (std::size_t i = 0; i + 4 <= aSize; i += 4)
    {
        std::uint32_t w;
        std::memcpy(&w, aData.data() + aOffset + i, sizeof(w));
        aOut.insert(w);
    }
}
} // namespace

std::optional<std::unordered_set<std::uint32_t>> PluginRequirements::CollectConstants(const std::filesystem::path& aPath)
{
    std::ifstream file(aPath, std::ios::binary);
    if (!file)
    {
        return std::nullopt;
    }
    const std::vector<char> data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

    mach_header_64 header{};
    if (!Read(data, 0, header) || header.magic != MH_MAGIC_64)
    {
        return std::nullopt;
    }

    std::unordered_set<std::uint32_t> constants;
    std::size_t offset = sizeof(mach_header_64);
    for (std::uint32_t i = 0; i < header.ncmds; i++)
    {
        load_command cmd{};
        if (!Read(data, offset, cmd) || cmd.cmdsize < sizeof(load_command))
        {
            return std::nullopt;
        }

        if (cmd.cmd == LC_SEGMENT_64)
        {
            segment_command_64 segment{};
            if (!Read(data, offset, segment))
            {
                return std::nullopt;
            }

            for (std::uint32_t s = 0; s < segment.nsects; s++)
            {
                section_64 section{};
                if (!Read(data, offset + sizeof(segment_command_64) + s * sizeof(section_64), section))
                {
                    return std::nullopt;
                }
                if (section.offset > data.size() || data.size() - section.offset < section.size)
                {
                    continue; // zerofill or out of file
                }

                const std::string_view seg(section.segname, strnlen(section.segname, sizeof(section.segname)));
                const std::string_view name(section.sectname, strnlen(section.sectname, sizeof(section.sectname)));
                if (seg == "__TEXT" && name == "__text")
                {
                    ScanCode(data, section.offset, section.size, constants);
                }
                else if ((seg == "__TEXT" && (name == "__const" || name == "__literal4")) ||
                         (seg == "__DATA_CONST" && name == "__const") || (seg == "__DATA" && name == "__data"))
                {
                    ScanData(data, section.offset, section.size, constants);
                }
            }
        }

        offset += cmd.cmdsize;
    }

    return constants;
}
#endif
