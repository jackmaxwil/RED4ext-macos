#include "EnhancedMockGameBinary.hpp"

#ifdef RED4EXT_PLATFORM_MACOS
#include "Platform/RuntimeValidation.hpp"

namespace TestFramework
{
    EnhancedMockGameBinary::EnhancedMockGameBinary()
        : m_prologueDb(&PrologueDatabase::GetInstance())
    {
    }

    EnhancedMockGameBinary::~EnhancedMockGameBinary()
    {
    }

    void* EnhancedMockGameBinary::CreateAccurateMockFunction(const std::string& aName, 
                                                              AccurateMockGenerator::PrologueType aType)
    {
        void* func = AccurateMockGenerator::CreateMockFunctionWithValidPrologue(aType);
        if (func)
        {
            m_functions[aName] = func;
            
            // Store prologue pattern for validation
            auto patterns = AccurateMockGenerator::GetKnownValidPrologues();
            if (!patterns.empty())
            {
                m_functionPrologues[aName] = patterns[0]; // Use first pattern
            }
        }
        return func;
    }

    void* EnhancedMockGameBinary::CreateMockFunctionFromRealPrologue(const std::string& aName, uint32_t aHash)
    {
        ProloguePattern pattern = m_prologueDb->GetPrologueForHash(aHash);
        return CreateMockFunctionWithPattern(aName, pattern);
    }

    void* EnhancedMockGameBinary::CreateMockFunctionWithPattern(const std::string& aName, 
                                                                 const ProloguePattern& aPattern)
    {
        void* func = AccurateMockGenerator::CreateMockFunctionWithPrologue(aPattern);
        if (func)
        {
            m_functions[aName] = func;
            m_functionPrologues[aName] = aPattern;
        }
        return func;
    }

    bool EnhancedMockGameBinary::ValidateAllMocks() const
    {
        for (const auto& [name, func] : m_functions)
        {
            auto it = m_functionPrologues.find(name);
            if (it != m_functionPrologues.end())
            {
                if (!AccurateMockGenerator::ValidateMockPrologue(func, it->second))
                {
                    return false;
                }
            }
            else
            {
                // Validate using RuntimeValidation
                std::string reason;
                if (!Platform::RuntimeValidation::ValidateFunctionPrologue(func, reason))
                {
                    return false;
                }
            }
        }
        return true;
    }

    void EnhancedMockGameBinary::LoadPrologueDatabase(const std::string& aGamePath)
    {
        m_prologueDb->LoadFromGameBinary(aGamePath);
    }

    void EnhancedMockGameBinary::SavePrologueDatabase(const std::string& aFilePath) const
    {
        m_prologueDb->SaveToFile(aFilePath);
    }
}
#endif
