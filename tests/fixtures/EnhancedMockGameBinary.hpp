#pragma once

#ifdef RED4EXT_PLATFORM_MACOS
#include "AccurateMockGenerator.hpp"
#include "MockGameBinary.hpp"
#include <memory>
#include <unordered_map>
#include <string>
#include <vector>

namespace TestFramework
{
    // Enhanced mock game binary that uses accurate prologue patterns
    class EnhancedMockGameBinary : public MockGameBinary
    {
    public:
        EnhancedMockGameBinary();
        ~EnhancedMockGameBinary();

        // Create mock function with accurate prologue (validates against RuntimeValidation)
        void* CreateAccurateMockFunction(const std::string& aName, 
                                         AccurateMockGenerator::PrologueType aType = 
                                         AccurateMockGenerator::PrologueType::Standard);

        // Create mock function using prologue from real game binary
        void* CreateMockFunctionFromRealPrologue(const std::string& aName, uint32_t aHash);

        // Create mock function with custom prologue pattern
        void* CreateMockFunctionWithPattern(const std::string& aName, 
                                            const ProloguePattern& aPattern);

        // Validate all created mock functions
        bool ValidateAllMocks() const;

        // Load prologue database from game binary
        void LoadPrologueDatabase(const std::string& aGamePath);

        // Save prologue database for reuse
        void SavePrologueDatabase(const std::string& aFilePath) const;

    private:
        PrologueDatabase* m_prologueDb;
        std::unordered_map<std::string, ProloguePattern> m_functionPrologues;
    };

    // Helper: Create mock that exactly matches game binary behavior
    class GameBinaryMimic
    {
    public:
        // Analyze real game binary and create mocks that match
        static std::unique_ptr<EnhancedMockGameBinary> CreateFromGameBinary(const std::string& aGamePath);

        // Extract address ranges from game binary
        struct AddressRange
        {
            void* start;
            void* end;
            std::string segment;
        };
        static std::vector<AddressRange> ExtractAddressRanges(const std::string& aGamePath);

        // Extract function addresses and prologues
        struct FunctionInfo
        {
            uint32_t hash;
            void* address;
            ProloguePattern prologue;
            std::string symbol;
        };
        static std::vector<FunctionInfo> ExtractFunctions(const std::string& aGamePath, 
                                                          const std::vector<uint32_t>& aHashes);
    };
}
#endif
