#include "Platform/CrashHandler.hpp"
#include "Platform/RuntimeValidation.hpp"
#include "fixtures/MockGameBinary.hpp"
#include "lib/TestFramework.hpp"

#ifdef RED4EXT_PLATFORM_MACOS

void RegisterSuite_RuntimeValidation(TestFramework::TestSuite& suite);

void RegisterSuite_RuntimeValidation(TestFramework::TestSuite& suite)
{
    suite.AddTest("ValidateInvalidAddress",
                  []()
                  {
                      void* invalidAddr = nullptr;
                      std::string reason;
                      bool isValid = Platform::RuntimeValidation::ValidateFunctionPrologue(invalidAddr, reason);
                      TestFramework::Assert(!isValid, "Null address should be invalid");
                  });

    suite.AddTest("TestFrameworkWorks",
                  []()
                  {
                      // Basic test to verify framework works
                      TestFramework::Assert(true, "Framework is working");
                      TestFramework::AssertEqual(1 + 1, 2, "Basic arithmetic");
                  });
}

int main()
{
    TestFramework::TestRunner::GetInstance().RegisterSuite("RuntimeValidation", RegisterSuite_RuntimeValidation);
    return TestFramework::TestRunner::GetInstance().RunAll();
}

#endif
