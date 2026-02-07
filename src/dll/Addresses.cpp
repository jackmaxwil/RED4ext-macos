#include "Addresses.hpp"
#include "Platform.hpp"
#include "Platform/CrashHandler.hpp"
#include "Platform/StructuredLogging.hpp"

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
        // Fallback: preserve existing behavior.
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
}

Addresses::Addresses(const Paths& aPaths)
{
    constexpr auto filename = L"cyberpunk2077_addresses.json";
    auto filePath = aPaths.GetX64Dir() / filename;

    LoadSections();
#ifdef RED4EXT_PLATFORM_MACOS
    constexpr auto symbolsFilename = L"cyberpunk2077_symbols.json";
    auto symbolsPath = aPaths.GetX64Dir() / symbolsFilename;
    LoadSymbols(symbolsPath);
#endif
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
    // Prefer the address database when present (it covers non-exported functions and
    // avoids dlsym resolving to branch islands/trampolines).
    const auto it = m_addresses.find(aHash);
    if (it != m_addresses.end() && it->second != 0)
    {
        void* resolvedAddr = reinterpret_cast<void*>(it->second);
        
        // Validate resolved address
        if (!Platform::CrashHandler::IsValidAddress(resolvedAddr, 16))
        {
            Platform::CrashHandler::MemoryRegionInfo regionInfo;
            bool hasRegion = Platform::CrashHandler::GetMemoryRegionInfo(resolvedAddr, regionInfo);
            
            Log::warn("[Addresses] Resolved hash 0x{:08X} to INVALID address {} (not in mapped region)", 
                      aHash, fmt::ptr(resolvedAddr));
            if (hasRegion)
            {
                Log::warn("[Addresses] Nearest region: {} size={} prot={:#x} name={}", 
                          fmt::ptr(regionInfo.start), regionInfo.size, regionInfo.protection, regionInfo.name);
            }
            // Still return it - let the hook system validate
        }
        
        return it->second;
    }

    // First, try to resolve via symbol name (if we have a mapping)
    const auto symIt = m_hashToSymbol.find(aHash);
    if (symIt != m_hashToSymbol.end())
    {
        // Resolve symbols against the main executable image (avoid RTLD_DEFAULT collisions
        // with plugin exports when plugins are loaded with RTLD_GLOBAL).
        static Platform::Handle s_mainHandle = Platform::GetModuleHandle(nullptr);
        void* addr = Platform::GetProcAddress(s_mainHandle, symIt->second.c_str());
        if (addr)
        {
            Log::trace("Resolved hash 0x{:08X} via symbol '{}' to address {}", 
                         aHash, symIt->second, fmt::ptr(addr));
            Platform::StructuredLogging::LogStructured(spdlog::level::trace, "[Addresses]", 
                fmt::format("Resolved hash 0x{:08X} via symbol '{}' to {}", aHash, symIt->second, fmt::ptr(addr)));
            Platform::StructuredLogging::PopContext();
            return reinterpret_cast<std::uintptr_t>(addr);
        }
        else
        {
            Log::warn("Symbol '{}' (hash 0x{:08X}) not found via dlsym", 
                        symIt->second, aHash);
            Platform::StructuredLogging::LogStructured(spdlog::level::warn, "[Addresses]", 
                fmt::format("Symbol '{}' (hash 0x{:08X}) not found", symIt->second, aHash));
        }
    }
    
    // No database entry and symbol lookup failed.
    Log::warn("Could not resolve hash 0x{:08X} - no symbol mapping or address entry", aHash);
    Platform::StructuredLogging::LogStructured(spdlog::level::warn, "[Addresses]", 
        fmt::format("Could not resolve hash 0x{:08X}", aHash));
    Platform::StructuredLogging::PopContext();
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

void Addresses::LoadSymbols(const std::filesystem::path& aSymbolsPath)
{
    // Map RED4ext hashes to macOS mangled symbols
    // Load from JSON file generated by generate_symbol_mapping.py script
    
    if (!exists(aSymbolsPath))
    {
        Log::debug("Symbol mapping file not found at '{}', symbol resolution will use address database only", aSymbolsPath);
        return;
    }
    
    Log::info("Loading symbol mappings from '{}'...", aSymbolsPath);
    
    try
    {
        simdjson::ondemand::parser parser;
        simdjson::padded_string json = simdjson::padded_string::load(aSymbolsPath.string());
        simdjson::ondemand::document document = parser.iterate(json);
        
        simdjson::ondemand::array mappings;
        auto error = document["mappings"].get_array().get(mappings);
        if (error)
        {
            Log::warn("Could not get mappings array from symbol file: {}", 
                        simdjson::error_message(error));
            return;
        }
        
        mappings.reset();
        size_t loaded = 0;
        
        for (auto entry : mappings)
        {
            auto hashField = entry.find_field("hash");
            auto symbolField = entry.find_field("symbol");
            
            if (!hashField.error() && !symbolField.error())
            {
                std::string_view hashStr;
                error = hashField.get_string().get(hashStr);
                if (error)
                {
                    continue;
                }
                
                std::string_view symbolStr;
                error = symbolField.get_string().get(symbolStr);
                if (error)
                {
                    continue;
                }
                
                // Parse hash (format: "0x12345678")
                std::uint32_t hash = 0;
                if (hashStr.length() > 2 && hashStr.substr(0, 2) == "0x")
                {
                    std::stringstream stream;
                    stream << std::hex << hashStr.substr(2);
                    stream >> hash;
                }
                else
                {
                    continue;
                }
                
                // Store mapping
                m_hashToSymbol.emplace(hash, std::string(symbolStr));
                loaded++;
            }
        }
        
        Log::info("Loaded {} symbol mappings", loaded);
        Log::trace("Symbol mapping initialized ({} entries)", m_hashToSymbol.size());
    }
    catch (const std::exception& e)
    {
        Log::warn("Error loading symbol mappings: {}", e.what());
    }
}

void Addresses::LoadAddresses(const std::filesystem::path& aPath)
{
    if (!exists(aPath))
    {
#ifdef RED4EXT_PLATFORM_MACOS
        // On macOS, addresses.json is optional - we can use symbol mapping for exported functions
        Log::warn("Address database not found at '{}' - symbol resolution will use symbol mapping only", aPath);
        Log::warn("Non-exported functions will not be resolvable without the address database");
        return;
#else
        SHOW_MESSAGE_BOX_AND_EXIT_FILE_LINE(L"The addresses JSON does not exists\n\nPath: {}", aPath);
        return;
#endif
    }

    Log::info("Loading address database from: {}", aPath.string());

    simdjson::ondemand::parser parser;
    simdjson::padded_string json = simdjson::padded_string::load(aPath.string());
    simdjson::ondemand::document document = parser.iterate(json);

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
    // Offsets in our addresses.json are image-relative offsets from the __TEXT base.
    // Resolve = main executable image base + offset.
    const auto imageIdx = GetMainExecutableImageIndex();
    const auto* header = GetMainExecutableHeader();
    const auto slide = GetMainExecutableSlide();
    const char* imageName = _dyld_get_image_name(imageIdx);
    const auto imageBase = reinterpret_cast<std::uintptr_t>(header);

    Log::info("[Addresses] macOS dyld image[{}]='{}' base={} slide={:#x}",
              imageIdx, imageName ? imageName : "<null>", fmt::ptr(reinterpret_cast<void*>(imageBase)), slide);
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
            std::uintptr_t address = 0;

            switch (segment)
            {
            case 1:
                // Current databases: image-relative offset from __TEXT base.
                address = imageBase + offset;
                break;
            case 2:
                // Legacy/escape hatch: segment-relative (__DATA_CONST).
                address = imageBase + static_cast<std::uintptr_t>(m_rdataOffset) + offset;
                break;
            case 3:
                // Legacy/escape hatch: segment-relative (__DATA).
                address = imageBase + static_cast<std::uintptr_t>(m_dataOffset) + offset;
                break;
            default:
                Log::warn("Unknown segment {} for hash 0x{:08X}", segment, static_cast<std::uint32_t>(hash));
                address = 0;
                break;
            }
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

    uintptr_t cmdPtr = reinterpret_cast<uintptr_t>(header + 1);
    for (uint32_t i = 0; i < header->ncmds; i++)
    {
        const struct load_command* cmd = reinterpret_cast<const struct load_command*>(cmdPtr);
        if (cmd->cmd == LC_SEGMENT_64)
        {
            const struct segment_command_64* seg = reinterpret_cast<const struct segment_command_64*>(cmdPtr);
            if (strcmp(seg->segname, "__TEXT") == 0)
            {
                m_codeOffset = static_cast<uint32_t>(seg->vmaddr);
            }
            else if (strcmp(seg->segname, "__DATA") == 0)
            {
                m_dataOffset = static_cast<uint32_t>(seg->vmaddr);
            }
            else if (strcmp(seg->segname, "__DATA_CONST") == 0)
            {
                m_rdataOffset = static_cast<uint32_t>(seg->vmaddr);
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
