#include "MockGameBinary.hpp"

#ifdef RED4EXT_PLATFORM_MACOS
#include <cstdlib>
#include <cstring>
#include <libkern/OSCacheControl.h>
#include <sys/mman.h>
#include <unistd.h>

namespace TestFramework
{
MockGameBinary::MockGameBinary()
{
    // Use a mock base address (simulating game binary)
    m_baseAddress = reinterpret_cast<void*>(0x100000000ULL);
}

MockGameBinary::~MockGameBinary()
{
    // Free all allocated memory
    for (void* ptr : m_allocatedMemory)
    {
        FreeExecutableMemory(ptr);
    }
}

void* MockGameBinary::AllocateExecutableMemory(size_t aSize)
{
    // Allocate executable memory for mock functions
    void* mem = mmap(nullptr, aSize, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (mem == MAP_FAILED)
    {
        return nullptr;
    }
    m_allocatedMemory.push_back(mem);
    return mem;
}

void MockGameBinary::FreeExecutableMemory(void* aPtr)
{
    // Find size (we'd need to track this, but for tests we can use a fixed size)
    size_t size = 4096; // Page size
    munmap(aPtr, size);
}

std::vector<uint32_t> MockGameBinary::CreateValidPrologue()
{
    // Create a valid ARM64 function prologue: STP X29, X30, [SP, #-0x10]!
    // Encoding: 0xA9800000 | (reg1 << 10) | (reg2 << 5) | (imm << 0)
    // STP X29, X30, [SP, #-0x10]! = 0xA9800000 | (29 << 10) | (30 << 5) | (0x10 >> 2)
    std::vector<uint32_t> prologue;
    prologue.push_back(0xA9800000 | (29 << 10) | (30 << 5) | (0x10 >> 2)); // STP X29, X30, [SP, #-0x10]!
    prologue.push_back(0xD10003FF);                                        // SUB SP, SP, #0x10
    return prologue;
}

void* MockGameBinary::CreateMockFunction(const std::string& aName, void* aImplementation)
{
    if (aImplementation)
    {
        m_functions[aName] = aImplementation;
        return aImplementation;
    }

    // Create a simple function that returns a value
    void* mem = AllocateExecutableMemory(64);
    if (!mem)
    {
        return nullptr;
    }

    // Write ARM64 function prologue
    auto prologue = CreateValidPrologue();
    std::memcpy(mem, prologue.data(), prologue.size() * sizeof(uint32_t));

    // Write function body (MOV X0, #1; RET)
    uint32_t* code = reinterpret_cast<uint32_t*>(mem);
    code[2] = 0xD2800020; // MOV X0, #1
    code[3] = 0xD65F03C0; // RET

    // Flush instruction cache
    sys_icache_invalidate(mem, 64);

    m_functions[aName] = mem;
    return mem;
}

void* MockGameBinary::GetFunction(const std::string& aName) const
{
    auto it = m_functions.find(aName);
    return it != m_functions.end() ? it->second : nullptr;
}

void MockGameBinary::RegisterFunction(uint32_t aHash, const std::string& aName, void* aAddress)
{
    m_hashToFunction[aHash] = {aName, aAddress};
    m_functions[aName] = aAddress;
}

void* MockGameBinary::GetAddressForHash(uint32_t aHash) const
{
    auto it = m_hashToFunction.find(aHash);
    return it != m_hashToFunction.end() ? it->second.second : nullptr;
}

void* MockGameBinary::CreateInvalidFunction()
{
    // Create memory that's not executable (for negative tests)
    void* mem = mmap(nullptr, 64, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (mem == MAP_FAILED)
    {
        return nullptr;
    }
    std::memset(mem, 0, 64);
    m_allocatedMemory.push_back(mem);
    return mem;
}

std::string MockGameBinary::GetSymbolForHash(uint32_t aHash) const
{
    auto it = m_hashToFunction.find(aHash);
    return it != m_hashToFunction.end() ? it->second.first : "";
}

// Test function implementations
extern "C" int TestFunction_Simple()
{
    return 42;
}

extern "C" int TestFunction_WithArgs(int a, int b)
{
    return a + b;
}

extern "C" void* TestFunction_ReturnsPointer()
{
    static int value = 0x12345678;
    return &value;
}
} // namespace TestFramework
#endif
