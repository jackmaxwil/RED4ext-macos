#pragma once

#ifdef RED4EXT_PLATFORM_MACOS
#include <cstdint>
#include <dlfcn.h>
#include <mach-o/dyld.h>
#include <string>
#include <unordered_map>
#include <vector>

namespace TestFramework
{
// Mock game binary for testing hook attachment and address resolution
class MockGameBinary
{
public:
    MockGameBinary();
    ~MockGameBinary();

    // Create a mock function that can be hooked
    void* CreateMockFunction(const std::string& aName, void* aImplementation = nullptr);

    // Get a function by name
    void* GetFunction(const std::string& aName) const;

    // Register a function with a hash
    void RegisterFunction(uint32_t aHash, const std::string& aName, void* aAddress);

    // Get address for a hash
    void* GetAddressForHash(uint32_t aHash) const;

    // Create a valid ARM64 function prologue
    static std::vector<uint32_t> CreateValidPrologue();

    // Create an invalid function (for negative tests)
    void* CreateInvalidFunction();

    // Get the mock binary's base address
    void* GetBaseAddress() const
    {
        return m_baseAddress;
    }

    // Get symbol name for hash
    std::string GetSymbolForHash(uint32_t aHash) const;

protected: // Changed to protected so EnhancedMockGameBinary can access
    void* m_baseAddress;
    std::unordered_map<std::string, void*> m_functions;
    std::unordered_map<uint32_t, std::pair<std::string, void*>> m_hashToFunction;
    std::vector<void*> m_allocatedMemory;

    void* AllocateExecutableMemory(size_t aSize);
    void FreeExecutableMemory(void* aPtr);
};

// Helper: Create a simple test function
extern "C" int TestFunction_Simple();
extern "C" int TestFunction_WithArgs(int a, int b);
extern "C" void* TestFunction_ReturnsPointer();
} // namespace TestFramework
#endif
