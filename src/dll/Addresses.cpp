#include "Addresses.hpp"
#include "Platform.hpp"

#include <ios>
#include <string>
#include <string_view>

#ifdef RED4EXT_PLATFORM_MACOS
#include <mach-o/dyld.h>
#include <mach-o/loader.h>
#endif

#include <RED4ext/Relocation.hpp>
#include <spdlog/spdlog.h>

// simdjson is included in stdafx.hpp (precompiled header)

#include "Utils.hpp"

namespace
{
std::unique_ptr<Addresses> g_addresses;

#ifdef RED4EXT_PLATFORM_MACOS
uint32_t GetMainExecutableImageIndex()
{
    // When injected via DYLD_INSERT_LIBRARIES, dyld image ordering can vary.
    // Never assume image 0 is the game executable; instead, find the image whose
    // filename matches the current process executable.
    static uint32_t s_index = UINT32_MAX;
    if (s_index != UINT32_MAX)
    {
        return s_index;
    }

    const auto exePath = Platform::GetModuleFileName(nullptr);
    const auto exeFile = exePath.filename();

    const uint32_t count = _dyld_image_count();
    for (uint32_t i = 0; i < count; ++i)
    {
        const char* name = _dyld_get_image_name(i);
        if (!name || !*name)
        {
            continue;
        }

        if (std::filesystem::path(name).filename() == exeFile)
        {
            s_index = i;
            break;
        }
    }

    if (s_index == UINT32_MAX)
    {
        s_index = 0;
    }

    return s_index;
}

const mach_header_64* GetMainExecutableHeader()
{
    const auto idx = GetMainExecutableImageIndex();
    return reinterpret_cast<const mach_header_64*>(_dyld_get_image_header(idx));
}

intptr_t GetMainExecutableSlide()
{
    const auto idx = GetMainExecutableImageIndex();
    return _dyld_get_image_vmaddr_slide(idx);
}
#endif
} // namespace

Addresses::Addresses(const Paths& aPaths)
{
    constexpr auto filename = L"cyberpunk2077_addresses.json";
    auto filePath = aPaths.GetX64Dir() / filename;

    LoadSections();
    LoadAddresses(filePath);
}

void Addresses::Construct(const Paths& aPaths)
{
    g_addresses.reset(new Addresses(aPaths));
}

Addresses* Addresses::Instance()
{
    return g_addresses.get();
}

std::uintptr_t Addresses::Resolve(std::uint32_t aHash) const
{
#ifdef RED4EXT_PLATFORM_MACOS
    // Only the verified address database: LoadAddresses keeps verified entries only.
    const auto it = m_addresses.find(aHash);
    if (it != m_addresses.end())
    {
        // Addresses in the database are already resolved (base + slide + offset)
        return it->second;
    }

    Log::warn("Could not resolve hash 0x{:08X}: no verified address entry", aHash);
    return 0;
#else
    const auto it = m_addresses.find(aHash);
    if (it == m_addresses.end())
    {
        return 0;
    }

    const auto address = it->second;
    return address;
#endif
}

const std::string& Addresses::GetDatabaseGameVersion() const
{
    return m_dbGameVersion;
}

const std::string& Addresses::GetDatabaseUuid() const
{
    return m_dbUuid;
}

void Addresses::LoadAddresses(const std::filesystem::path& aPath)
{
    if (!exists(aPath))
    {
#ifdef RED4EXT_PLATFORM_MACOS
        // Without the database nothing resolves; RED4ext then refuses every plugin that needs an address.
        Log::error("Address database not found at '{}': no game address will resolve", aPath);
        return;
#else
        SHOW_MESSAGE_BOX_AND_EXIT_FILE_LINE(L"The addresses JSON does not exists\n\nPath: {}", aPath);
        return;
#endif
    }

    Log::info("Loading game's addresses from '{}'...", aPath.string());

    simdjson::ondemand::parser parser;
    simdjson::padded_string json = simdjson::padded_string::load(aPath.string());
    simdjson::ondemand::document document = parser.iterate(json);

    // Optional metadata used for runtime compatibility checks.
#ifdef RED4EXT_PLATFORM_MACOS
    document.rewind();
    {
        std::string_view uuid{};
        auto uuidErr = document["uuid"].get_string().get(uuid);
        if (!uuidErr)
        {
            m_dbUuid.assign(uuid.data(), uuid.size());
        }
        else
        {
            m_dbUuid.clear();
        }
    }
    document.rewind();
#endif
    {
        std::string_view gameVersion{};
        auto versionErr = document["game_version"].get_string().get(gameVersion);
        if (!versionErr)
        {
            m_dbGameVersion.assign(gameVersion.data(), gameVersion.size());
            Log::info("Address DB game_version: {}", m_dbGameVersion);
        }
        else
        {
            m_dbGameVersion.clear();
        }
    }

    simdjson::ondemand::array root;
    auto error = document["Addresses"].get_array().get(root);
    if (error)
    {
#ifdef RED4EXT_PLATFORM_MACOS
        Log::error("Could not get the root array for the addresses: {}", simdjson::error_message(error));
        exit(1);
#else
        SHOW_MESSAGE_BOX_AND_EXIT_FILE_LINE(L"Could not get the root array for the addresses: {}",
                                            Utils::Widen(simdjson::error_message(error)));
#endif
        return;
    }

#ifdef RED4EXT_PLATFORM_MACOS
    const auto imgIdx = GetMainExecutableImageIndex();
    Log::info("[Addresses] macOS dyld image[{}]='{}' base=0x{:x} slide=0x{:x}", imgIdx, _dyld_get_image_name(imgIdx),
              reinterpret_cast<std::uintptr_t>(_dyld_get_image_header(imgIdx)),
              static_cast<std::uintptr_t>(_dyld_get_image_vmaddr_slide(imgIdx)));
    Log::info("[Addresses] Segment bases: TEXT=0x{:x} DATA_CONST=0x{:x} DATA=0x{:x}", m_codeOffset, m_rdataOffset,
              m_dataOffset);
#else
    auto base = reinterpret_cast<std::uintptr_t>(Platform::GetModuleHandle(nullptr));
#endif

    root.reset();

    for (auto entry : root)
    {
        auto hashField = entry.find_field("hash");
        auto offsetField = entry.find_field("offset");

        if (!hashField.error() && !offsetField.error())
        {
            std::uint64_t hash;
            error = hashField.get_uint64_in_string().get(hash);
            if (error)
            {
#ifdef RED4EXT_PLATFORM_MACOS
                Log::error("Could not get the hash for an address: {}", simdjson::error_message(error));
                exit(1);
#else
                SHOW_MESSAGE_BOX_AND_EXIT_FILE_LINE(L"Could not get the hash for an address: {}",
                                                    Utils::Widen(simdjson::error_message(error)));
#endif
                return;
            }

            std::string_view offsetStr;
            error = offsetField.get_string().get(offsetStr);
            if (error)
            {
#ifdef RED4EXT_PLATFORM_MACOS
                Log::error("Could not get the offset for an address: {}", simdjson::error_message(error));
                exit(1);
#else
                SHOW_MESSAGE_BOX_AND_EXIT_FILE_LINE(L"Could not get the offset for an address: {}",
                                                    Utils::Widen(simdjson::error_message(error)));
#endif
                return;
            }

            std::stringstream stream;
            stream << offsetStr;

            std::uint32_t segment;
            char separator;
            std::uint32_t offset;
            stream >> std::hex >> segment >> separator >> offset;

#ifdef RED4EXT_PLATFORM_MACOS
            // Fail closed: only entries the address audit marked "verified": true resolve. An unverified address
            // can point into the middle of unrelated code or data. RED4EXT_ALLOW_UNVERIFIED_ADDRESSES=1 overrides
            // this for reverse-engineering sessions only.
            static const bool allowUnverified = []()
            {
                const char* env = std::getenv("RED4EXT_ALLOW_UNVERIFIED_ADDRESSES");
                return env && *env == '1';
            }();
            bool verified = false;
            if (auto verifiedField = entry.find_field("verified"); !verifiedField.error())
            {
                verifiedField.get_bool().get(verified);
            }
            if (!verified && !allowUnverified)
            {
                Log::debug("Address for hash 0x{:08X} is not verified; leaving it unresolved", hash);
                m_addresses.emplace(static_cast<std::uint32_t>(hash), 0);
                continue;
            }

            if (offset == 0)
            {
                // Explicitly allow zero offsets (GPU-only or unsupported on macOS)
                m_addresses.emplace(static_cast<std::uint32_t>(hash), 0);
                continue;
            }

            // On macOS, offsets in the JSON are relative to segment start.
            // m_codeOffset / m_rdataOffset / m_dataOffset store the runtime
            // segment base (vmaddr + ASLR slide), so final address is simply
            // segmentBase + offset.
            std::uintptr_t segmentBase = 0;

            switch (segment)
            {
            case 1: // __TEXT segment (code)
                segmentBase = m_codeOffset;
                break;
            case 2: // __DATA_CONST segment (read-only data)
                segmentBase = m_rdataOffset;
                break;
            case 3: // __DATA segment (read-write data)
                segmentBase = m_dataOffset;
                break;
            default:
                Log::warn("Unknown segment {} for hash 0x{:08X}", segment, hash);
                break;
            }

            auto address = segmentBase + offset;
#else
            switch (segment)
            {
            case 1:
                offset += m_codeOffset;
                break;
            case 2:
                offset += m_rdataOffset;
                break;
            case 3:
                offset += m_dataOffset;
                break;
            }

            auto address = offset + base;
#endif
            m_addresses.emplace(static_cast<std::uint32_t>(hash), address);
        }
    }

    Log::info("{} game addresses loaded", m_addresses.size());
}

void Addresses::LoadSections()
{
#ifdef RED4EXT_PLATFORM_MACOS
    const struct mach_header_64* header = GetMainExecutableHeader();
    if (header == nullptr)
    {
        Log::error("Error: Could not get Mach-O header.");
        exit(1);
        return;
    }

    const auto slide = GetMainExecutableSlide();

    uintptr_t cmdPtr = reinterpret_cast<uintptr_t>(header + 1);
    for (uint32_t i = 0; i < header->ncmds; i++)
    {
        const struct load_command* cmd = reinterpret_cast<const struct load_command*>(cmdPtr);
        if (cmd->cmd == LC_SEGMENT_64)
        {
            const struct segment_command_64* seg = reinterpret_cast<const struct segment_command_64*>(cmdPtr);
            // Store runtime segment base (vmaddr + ASLR slide)
            const auto runtimeBase = static_cast<std::uintptr_t>(static_cast<std::intptr_t>(seg->vmaddr) + slide);
            if (strcmp(seg->segname, "__TEXT") == 0)
            {
                m_codeOffset = runtimeBase;
            }
            else if (strcmp(seg->segname, "__DATA") == 0)
            {
                m_dataOffset = runtimeBase;
            }
            else if (strcmp(seg->segname, "__DATA_CONST") == 0)
            {
                m_rdataOffset = runtimeBase;
            }
        }
        cmdPtr += cmd->cmdsize;
    }
#else
    HMODULE hModule = Platform::GetModuleHandle(NULL);
    if (hModule == NULL)
    {
        SHOW_MESSAGE_BOX_AND_EXIT_FILE_LINE(L"Error: Could not get module handle.");
        return;
    }

    // Access the DOS header
    IMAGE_DOS_HEADER* dosHeader = (IMAGE_DOS_HEADER*)hModule;
    // Access the PE header
    IMAGE_NT_HEADERS* peHeader = (IMAGE_NT_HEADERS*)((BYTE*)hModule + dosHeader->e_lfanew);

    // Check for PE signature
    if (peHeader->Signature != IMAGE_NT_SIGNATURE)
    {
        SHOW_MESSAGE_BOX_AND_EXIT_FILE_LINE(L"Error: PE signature not found.");
        return;
    }

    // Access the section headers
    IMAGE_SECTION_HEADER* sectionHeaders = IMAGE_FIRST_SECTION(peHeader);
    const int numberOfSections = peHeader->FileHeader.NumberOfSections;

    // List the sections
    for (int i = 0; i < numberOfSections; i++)
    {
        IMAGE_SECTION_HEADER* sectionHeader = &sectionHeaders[i];
        if (strcmp(reinterpret_cast<const char*>(sectionHeader->Name), ".text") == 0)
            m_codeOffset = sectionHeader->VirtualAddress;
        else if (strcmp(reinterpret_cast<const char*>(sectionHeader->Name), ".data") == 0)
            m_dataOffset = sectionHeader->VirtualAddress;
        else if (strcmp(reinterpret_cast<const char*>(sectionHeader->Name), ".rdata") == 0)
            m_rdataOffset = sectionHeader->VirtualAddress;
    }
#endif
}

RED4EXT_C_EXPORT std::uintptr_t RED4EXT_CALL RED4ext_ResolveAddress(const std::uint32_t aHash)
{
    return Addresses::Instance()->Resolve(aHash);
}

std::vector<std::uint32_t> Addresses::UnresolvedAmong(const std::unordered_set<std::uint32_t>& aConstants) const
{
    std::vector<std::uint32_t> unresolved;
    for (const auto& [hash, address] : m_addresses)
    {
        if (address == 0 && aConstants.contains(hash))
        {
            unresolved.push_back(hash);
        }
    }
    std::sort(unresolved.begin(), unresolved.end());
    return unresolved;
}
