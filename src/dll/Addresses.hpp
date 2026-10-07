#pragma once

#include <cstdint>
#include <filesystem>
#include <unordered_map>

#include "Paths.hpp"

class Addresses
{
public:
    static void Construct(const Paths& aPaths);
    static Addresses* Instance();

    ~Addresses() = default;

    std::uintptr_t Resolve(std::uint32_t aHash) const;
    const std::string& GetDatabaseGameVersion() const;
    const std::string& GetDatabaseUuid() const;
    const std::string& SymbolForHash(std::uint32_t aHash) const;

private:
    Addresses(const Paths& aPaths);

    void LoadAddresses(const std::filesystem::path& aPath);
    void LoadSections();
    void LoadSymbols(const std::filesystem::path& aSymbolsPath);

    std::uintptr_t m_codeOffset;
    std::uintptr_t m_dataOffset;
    std::uintptr_t m_rdataOffset;
    std::unordered_map<std::uint32_t, std::uintptr_t> m_addresses;
    std::unordered_map<std::uint32_t, std::string> m_hashToSymbol;
    std::string m_dbGameVersion;
    std::string m_dbUuid;
};
