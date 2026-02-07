#include "AccurateMockGenerator.hpp"
#include "Platform/RuntimeValidation.hpp"
#include <sys/mman.h>
#include <libkern/OSCacheControl.h>
#include <cstring>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cstdlib>

#ifdef RED4EXT_PLATFORM_MACOS

namespace TestFramework
{
    // Use the exact same patterns as RuntimeValidation
    ProloguePattern AccurateMockGenerator::CreateStandardPrologue()
    {
        ProloguePattern pattern;
        // STP X29, X30, [SP, #-0x10]!
        // Encoding: 0xA9800000 | (29 << 10) | (30 << 5) | (0x10 >> 2)
        // Mask: 0xFFC00000, Value: 0xA9800000
        pattern.instructions.push_back(0xA9800000 | (29 << 10) | (30 << 5) | (0x10 >> 2));
        pattern.description = "STP X29,X30 frame save";
        pattern.isValid = true;
        return pattern;
    }

    ProloguePattern AccurateMockGenerator::CreateStackAllocPrologue(uint32_t aStackSize)
    {
        ProloguePattern pattern;
        // SUB SP, SP, #imm
        // Encoding: 0xD1000000 | (imm << 0)
        // Mask: 0xFF800000, Value: 0xD1000000
        uint32_t imm = aStackSize & 0x7FF; // 11-bit immediate
        pattern.instructions.push_back(0xD1000000 | (imm << 0));
        pattern.description = "SUB SP stack allocation";
        pattern.isValid = true;
        return pattern;
    }

    ProloguePattern AccurateMockGenerator::CreateFrameSetupPrologue()
    {
        ProloguePattern pattern;
        // MOV X29, SP
        // Encoding: 0xAA1F03FD
        // Mask: 0xFFE0001F, Value: 0xAA1F03FD
        pattern.instructions.push_back(0xAA1F03FD);
        pattern.description = "MOV X29,SP";
        pattern.isValid = true;
        return pattern;
    }

    ProloguePattern AccurateMockGenerator::CreateMinimalPrologue()
    {
        // Minimal valid prologue: just STP X29, X30
        return CreateStandardPrologue();
    }

    std::vector<ProloguePattern> AccurateMockGenerator::GetKnownValidPrologues()
    {
        std::vector<ProloguePattern> patterns;
        
        // These match exactly what RuntimeValidation expects
        patterns.push_back(CreateStandardPrologue());
        patterns.push_back(CreateStackAllocPrologue(0x10));
        patterns.push_back(CreateStackAllocPrologue(0x20));
        patterns.push_back(CreateStackAllocPrologue(0x30));
        patterns.push_back(CreateFrameSetupPrologue());
        
        return patterns;
    }

    ProloguePattern AccurateMockGenerator::ExtractPrologueFromGame(void* aFunctionAddress, size_t aMaxInstructions)
    {
        ProloguePattern pattern;
        pattern.isValid = false;

        if (!aFunctionAddress)
        {
            return pattern;
        }

        // Read instructions from game binary
        // Note: This requires the address to be readable
        for (size_t i = 0; i < aMaxInstructions; ++i)
        {
            uint32_t instr = 0;
            void* addr = reinterpret_cast<char*>(aFunctionAddress) + (i * 4);
            
            // Check if address is readable (would need CrashHandler::IsValidAddress)
            std::memcpy(&instr, addr, sizeof(instr));
            
            pattern.instructions.push_back(instr);
            
            // Check if this matches a known prologue pattern
            std::string reason;
            if (Platform::RuntimeValidation::ValidateFunctionPrologue(addr, reason))
            {
                pattern.isValid = true;
                pattern.description = reason;
                break;
            }
        }

        return pattern;
    }

    ProloguePattern AccurateMockGenerator::ExtractPrologueFromSymbol(const std::string& aSymbolName, void* aGameBase)
    {
        ProloguePattern pattern;
        pattern.isValid = false;

        if (!aGameBase)
        {
            // Try to get game base from dyld
            const uint32_t imageCount = _dyld_image_count();
            for (uint32_t i = 0; i < imageCount; ++i)
            {
                const char* imageName = _dyld_get_image_name(i);
                if (imageName && std::string(imageName).find("Cyberpunk2077") != std::string::npos)
                {
                    aGameBase = reinterpret_cast<void*>(const_cast<struct mach_header_64*>(
                        reinterpret_cast<const struct mach_header_64*>(_dyld_get_image_header(i))));
                    break;
                }
            }
        }

        if (!aGameBase)
        {
            return pattern;
        }

        // Resolve symbol
        void* symbolAddr = dlsym(RTLD_DEFAULT, aSymbolName.c_str());
        if (!symbolAddr)
        {
            return pattern;
        }

        return ExtractPrologueFromGame(symbolAddr);
    }

    void* AccurateMockGenerator::CreateMockFunctionWithPrologue(const ProloguePattern& aPrologue, 
                                                                 const std::vector<uint32_t>& aBody)
    {
        if (!aPrologue.isValid || aPrologue.instructions.empty())
        {
            return nullptr;
        }

        // Calculate total size needed
        size_t totalSize = (aPrologue.instructions.size() + aBody.size() + 2) * sizeof(uint32_t);
        totalSize = (totalSize + 4095) & ~4095; // Align to page size

        // Allocate executable memory
        void* mem = mmap(nullptr, totalSize, PROT_READ | PROT_WRITE | PROT_EXEC,
                        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (mem == MAP_FAILED)
        {
            return nullptr;
        }

        uint32_t* code = reinterpret_cast<uint32_t*>(mem);
        size_t offset = 0;

        // Write prologue
        for (uint32_t instr : aPrologue.instructions)
        {
            code[offset++] = instr;
        }

        // Write body (or default: MOV X0, #0; RET)
        if (aBody.empty())
        {
            code[offset++] = 0xD2800000; // MOV X0, #0
            code[offset++] = 0xD65F03C0; // RET
        }
        else
        {
            for (uint32_t instr : aBody)
            {
                code[offset++] = instr;
            }
        }

        // Flush instruction cache
        sys_icache_invalidate(mem, totalSize);

        return mem;
    }

    void* AccurateMockGenerator::CreateMockFunctionWithValidPrologue(PrologueType aType)
    {
        ProloguePattern pattern;
        
        switch (aType)
        {
        case PrologueType::Standard:
            pattern = CreateStandardPrologue();
            break;
        case PrologueType::StackAlloc:
            pattern = CreateStackAllocPrologue(0x10);
            break;
        case PrologueType::FrameSetup:
            pattern = CreateFrameSetupPrologue();
            break;
        case PrologueType::Minimal:
            pattern = CreateMinimalPrologue();
            break;
        }

        return CreateMockFunctionWithPrologue(pattern);
    }

    bool AccurateMockGenerator::ValidateMockPrologue(void* aFunction, const ProloguePattern& aExpected)
    {
        if (!aFunction || !aExpected.isValid)
        {
            return false;
        }

        // Read actual prologue
        std::vector<uint32_t> actual;
        for (size_t i = 0; i < aExpected.instructions.size(); ++i)
        {
            uint32_t instr = 0;
            std::memcpy(&instr, reinterpret_cast<char*>(aFunction) + (i * 4), sizeof(instr));
            actual.push_back(instr);
        }

        // Compare with expected
        if (actual.size() != aExpected.instructions.size())
        {
            return false;
        }

        for (size_t i = 0; i < actual.size(); ++i)
        {
            // Use the same mask matching as RuntimeValidation
            uint32_t mask = 0xFFC00000; // Default mask for STP
            if (i == 0 && aExpected.description.find("SUB SP") != std::string::npos)
            {
                mask = 0xFF800000; // Mask for SUB SP
            }
            else if (i == 0 && aExpected.description.find("MOV") != std::string::npos)
            {
                mask = 0xFFE0001F; // Mask for MOV
            }

            if ((actual[i] & mask) != (aExpected.instructions[i] & mask))
            {
                return false;
            }
        }

        // Also validate using RuntimeValidation
        std::string reason;
        return Platform::RuntimeValidation::ValidateFunctionPrologue(aFunction, reason);
    }

    std::vector<ProloguePattern> AccurateMockGenerator::AnalyzeGameBinaryPrologues(const std::string& aGamePath, 
                                                                                     size_t aSampleSize)
    {
        std::vector<ProloguePattern> patterns;
        
        // This would analyze the actual game binary to extract common prologue patterns
        // For now, return known valid patterns
        // In a full implementation, this would:
        // 1. Load the Mach-O binary
        // 2. Parse __TEXT segment
        // 3. Extract function starts
        // 4. Read prologues from each function
        // 5. Group by pattern
        // 6. Return most common patterns
        
        return GetKnownValidPrologues();
    }

    // PrologueDatabase implementation
    void PrologueDatabase::LoadFromGameBinary(const std::string& aGamePath)
    {
        // Extract prologues from real game binary
        auto patterns = AccurateMockGenerator::AnalyzeGameBinaryPrologues(aGamePath);
        
        // Store common patterns
        m_commonPatterns = patterns;
    }

    void PrologueDatabase::SaveToFile(const std::string& aFilePath) const
    {
        std::ofstream file(aFilePath);
        if (!file.is_open())
        {
            return;
        }

        file << "{\n";
        file << "  \"prologues\": [\n";
        
        for (size_t i = 0; i < m_commonPatterns.size(); ++i)
        {
            const auto& pattern = m_commonPatterns[i];
            if (i > 0) file << ",\n";
            file << "    {\n";
            file << "      \"description\": \"" << pattern.description << "\",\n";
            file << "      \"instructions\": [";
            for (size_t j = 0; j < pattern.instructions.size(); ++j)
            {
                if (j > 0) file << ", ";
                file << "\"0x" << std::hex << pattern.instructions[j] << std::dec << "\"";
            }
            file << "]\n";
            file << "    }";
        }
        
        file << "\n  ]\n";
        file << "}\n";
    }

    void PrologueDatabase::LoadFromFile(const std::string& aFilePath)
    {
        // Load prologues from JSON file
        // Implementation would parse JSON and populate m_commonPatterns
    }

    ProloguePattern PrologueDatabase::GetPrologueForHash(uint32_t aHash) const
    {
        auto it = m_prologues.find(aHash);
        if (it != m_prologues.end())
        {
            return it->second;
        }
        
        // Return a random common pattern
        if (!m_commonPatterns.empty())
        {
            return m_commonPatterns[0];
        }
        
        // Use public method to create standard prologue
        auto patterns = AccurateMockGenerator::GetKnownValidPrologues();
        return patterns.empty() ? ProloguePattern{} : patterns[0];
    }

    ProloguePattern PrologueDatabase::GetRandomPrologue() const
    {
        if (!m_commonPatterns.empty())
        {
            return m_commonPatterns[rand() % m_commonPatterns.size()];
        }
        
        // Use public method to create standard prologue
        auto patterns = AccurateMockGenerator::GetKnownValidPrologues();
        return patterns.empty() ? ProloguePattern{} : patterns[0];
    }

    void PrologueDatabase::AddPrologue(uint32_t aHash, const ProloguePattern& aPattern)
    {
        m_prologues[aHash] = aPattern;
    }
}
#endif
