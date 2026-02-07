#pragma once

#ifdef RED4EXT_PLATFORM_MACOS
#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <mach-o/loader.h>
#include <mach-o/dyld.h>
#include <dlfcn.h>

namespace TestFramework
{
    // Prologue pattern extracted from real game binary
    struct ProloguePattern
    {
        std::vector<uint32_t> instructions;
        std::string description;
        bool isValid;
    };

    // Accurate mock generator that uses real game binary patterns
    class AccurateMockGenerator
    {
    public:
        // Extract prologue from real function in game binary
        static ProloguePattern ExtractPrologueFromGame(void* aFunctionAddress, size_t aMaxInstructions = 8);

        // Extract prologue from symbol name in game binary
        static ProloguePattern ExtractPrologueFromSymbol(const std::string& aSymbolName, void* aGameBase = nullptr);

        // Create mock function using extracted prologue pattern
        static void* CreateMockFunctionWithPrologue(const ProloguePattern& aPrologue, 
                                                    const std::vector<uint32_t>& aBody = {});

        enum class PrologueType
        {
            Standard,      // STP X29, X30 frame save
            StackAlloc,    // SUB SP stack allocation
            FrameSetup,    // MOV X29, SP
            Minimal        // Minimal valid prologue
        };

        // Create mock function using one of the validated prologue patterns
        static void* CreateMockFunctionWithValidPrologue(PrologueType aType = PrologueType::Standard);

        // Validate mock function prologue matches expected patterns
        static bool ValidateMockPrologue(void* aFunction, const ProloguePattern& aExpected);

        // Get all known valid prologue patterns (from RuntimeValidation)
        static std::vector<ProloguePattern> GetKnownValidPrologues();

        // Analyze real game binary and extract common prologue patterns
        static std::vector<ProloguePattern> AnalyzeGameBinaryPrologues(const std::string& aGamePath, 
                                                                        size_t aSampleSize = 100);

    private:
        // Create standard prologue: STP X29, X30, [SP, #-0x10]!
        static ProloguePattern CreateStandardPrologue();

        // Create stack allocation prologue: SUB SP, SP, #imm
        static ProloguePattern CreateStackAllocPrologue(uint32_t aStackSize = 0x10);

        // Create frame setup prologue: MOV X29, SP
        static ProloguePattern CreateFrameSetupPrologue();

        // Create minimal valid prologue
        static ProloguePattern CreateMinimalPrologue();
    };

    // Prologue database - stores extracted patterns from real game
    class PrologueDatabase
    {
    public:
        static PrologueDatabase& GetInstance()
        {
            static PrologueDatabase instance;
            return instance;
        }

        // Load prologues from real game binary
        void LoadFromGameBinary(const std::string& aGamePath);

        // Save extracted prologues to file
        void SaveToFile(const std::string& aFilePath) const;

        // Load prologues from file
        void LoadFromFile(const std::string& aFilePath);

        // Get prologue pattern by hash
        ProloguePattern GetPrologueForHash(uint32_t aHash) const;

        // Get random prologue pattern
        ProloguePattern GetRandomPrologue() const;

        // Add prologue pattern
        void AddPrologue(uint32_t aHash, const ProloguePattern& aPattern);

    private:
        std::unordered_map<uint32_t, ProloguePattern> m_prologues;
        std::vector<ProloguePattern> m_commonPatterns;
    };
}
#endif
