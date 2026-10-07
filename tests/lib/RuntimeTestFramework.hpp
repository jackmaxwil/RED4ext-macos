#pragma once

#ifdef RED4EXT_PLATFORM_MACOS
#include <dlfcn.h>
#include <functional>
#include <mach-o/dyld.h>
#include <memory>
#include <string>
#include <vector>

namespace TestFramework
{
// Runtime test that runs against actual game binary
class RuntimeTest
{
public:
    RuntimeTest(const std::string& aName, std::function<bool()> aTestFunc)
        : m_name(aName)
        , m_testFunc(aTestFunc)
        , m_result(false)
    {
    }

    bool Run()
    {
        try
        {
            m_result = m_testFunc();
        }
        catch (...)
        {
            m_result = false;
        }
        return m_result;
    }

    const std::string& GetName() const
    {
        return m_name;
    }
    bool GetResult() const
    {
        return m_result;
    }

private:
    std::string m_name;
    std::function<bool()> m_testFunc;
    bool m_result;
};

// Game binary interface for runtime tests
class GameBinaryInterface
{
public:
    static GameBinaryInterface* Create(const std::string& aGamePath);

    // Get function address by hash
    void* GetFunctionByHash(uint32_t aHash) const;

    // Get function address by symbol name
    void* GetFunctionBySymbol(const std::string& aSymbol) const;

    // Get base address
    void* GetBaseAddress() const
    {
        return m_baseAddress;
    }

    // Validate address is in game binary
    bool IsValidGameAddress(void* aAddress) const;

    // Get loaded image info
    struct ImageInfo
    {
        void* baseAddress;
        size_t size;
        std::string path;
    };
    std::vector<ImageInfo> GetLoadedImages() const;

private:
    GameBinaryInterface(void* aBaseAddress)
        : m_baseAddress(aBaseAddress)
    {
    }
    void* m_baseAddress;
};

// Runtime test runner
class RuntimeTestRunner
{
public:
    static RuntimeTestRunner& GetInstance()
    {
        static RuntimeTestRunner instance;
        return instance;
    }

    void RegisterTest(const std::string& aName, std::function<bool()> aTestFunc)
    {
        m_tests.emplace_back(aName, aTestFunc);
    }

    int RunAll(const std::string& aGamePath)
    {
        auto game = GameBinaryInterface::Create(aGamePath);
        if (!game)
        {
            std::cerr << "Failed to load game binary: " << aGamePath << std::endl;
            return 1;
        }

        std::cout << "Running runtime tests against game binary..." << std::endl;
        std::cout << "Game base address: " << game->GetBaseAddress() << std::endl;

        size_t passed = 0;
        size_t failed = 0;

        for (auto& test : m_tests)
        {
            bool result = test.Run();
            if (result)
            {
                passed++;
                std::cout << "  [PASS] " << test.GetName() << std::endl;
            }
            else
            {
                failed++;
                std::cout << "  [FAIL] " << test.GetName() << std::endl;
            }
        }

        std::cout << "Results: " << passed << " passed, " << failed << " failed" << std::endl;
        return failed > 0 ? 1 : 0;
    }

private:
    std::vector<RuntimeTest> m_tests;
};

// Runtime test macros
#define RUNTIME_TEST(name)                                                                                             \
    void RegisterRuntimeTest_##name()                                                                                  \
    {                                                                                                                  \
            TestFramework::RuntimeTestRunner::GetInstance().RegisterTest(#name, []() -> bool {
#define END_RUNTIME_TEST                                                                                               \
    return true;                                                                                                       \
    });                                                                                                            \
    }
} // namespace TestFramework
#endif
