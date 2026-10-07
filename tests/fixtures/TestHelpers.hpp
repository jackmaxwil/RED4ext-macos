#pragma once

#ifdef RED4EXT_PLATFORM_MACOS
#include "AccurateMockGenerator.hpp"
#include "Addresses.hpp"
#include "MockGameBinary.hpp"
#include "Paths.hpp"
#include "Platform/CrashHandler.hpp"
#include "Platform/RuntimeValidation.hpp"
#include <filesystem>
#include <fstream>
#include <memory>
#include <utility>
#include <vector>

namespace TestFramework
{
// Helper functions for common test scenarios

// Create a test address database
inline std::unique_ptr<Addresses> CreateTestAddressDatabase(const std::filesystem::path& aTestDbPath)
{
    Paths paths(aTestDbPath.parent_path());
    auto addresses = std::make_unique<Addresses>(paths);
    addresses->LoadAddresses(aTestDbPath);
    return addresses;
}

// Create a mock function that passes RuntimeValidation
inline void* CreateValidatedMockFunction(const std::string& aName)
{
    EnhancedMockGameBinary game;
    void* func = game.CreateAccurateMockFunction(aName);

    // Validate it passes RuntimeValidation
    std::string reason;
    if (!Platform::RuntimeValidation::ValidateFunctionPrologue(func, reason))
    {
        return nullptr; // Failed validation
    }

    return func;
}

// Check if address is valid
inline bool IsValidAddress(void* aAddress)
{
    return Platform::CrashHandler::IsValidAddress(aAddress, 16);
}

// Check if address is executable
inline bool IsExecutableMemory(void* aAddress)
{
    return Platform::RuntimeValidation::ValidateExecutableRegion(aAddress, 16);
}

// Create test fixture for hook testing
class HookTestFixture
{
public:
    HookTestFixture()
        : m_game(std::make_unique<EnhancedMockGameBinary>())
    {
    }

    void* CreateHookableFunction(const std::string& aName)
    {
        return m_game->CreateAccurateMockFunction(aName);
    }

    bool ValidateFunction(void* aFunc)
    {
        std::string reason;
        return Platform::RuntimeValidation::ValidateFunctionPrologue(aFunc, reason);
    }

private:
    std::unique_ptr<EnhancedMockGameBinary> m_game;
};

// Create test fixture for address resolution
class AddressResolutionFixture
{
public:
    AddressResolutionFixture()
        : m_testDir(std::filesystem::temp_directory_path() / "red4ext_test")
    {
        std::filesystem::create_directories(m_testDir);
    }

    ~AddressResolutionFixture()
    {
        std::filesystem::remove_all(m_testDir);
    }

    std::filesystem::path CreateTestDatabase(const std::vector<std::pair<uint32_t, std::string>>& aAddresses)
    {
        auto dbPath = m_testDir / "test_addresses.json";
        std::ofstream db(dbPath);

        db << "{\n  \"Addresses\": [\n";
        for (size_t i = 0; i < aAddresses.size(); ++i)
        {
            if (i > 0)
                db << ",\n";
            db << "    {\n";
            db << "      \"hash\": \"" << aAddresses[i].first << "\",\n";
            db << "      \"offset\": \"" << aAddresses[i].second << "\"\n";
            db << "    }";
        }
        db << "\n  ]\n}\n";
        db.close();

        return dbPath;
    }

    std::unique_ptr<Addresses> CreateAddresses(const std::filesystem::path& aDbPath)
    {
        Paths paths(m_testDir);
        auto addresses = std::make_unique<Addresses>(paths);
        addresses->LoadAddresses(aDbPath);
        return addresses;
    }

private:
    std::filesystem::path m_testDir;
};
} // namespace TestFramework
#endif
