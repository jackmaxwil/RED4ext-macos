// Main test runner that runs all test suites
#include "tests/lib/TestFramework.hpp"
#include "tests/lib/TestReporter.hpp"

// Include test suite implementations
#ifdef RED4EXT_PLATFORM_MACOS
// Test suites register themselves via static initialization
// We just need to include them to trigger registration

// Note: Individual test files have their own main() functions
// This file is for running all tests together

int main(int argc, char* argv[])
{
    // For now, just run individual test executables
    // In the future, this could aggregate all test suites
    std::cout << "Use individual test executables or CTest:\n";
    std::cout << "  ./UnitTests_Addresses\n";
    std::cout << "  ./UnitTests_Validation\n";
    std::cout << "  ./IntegrationTests\n";
    std::cout << "  ./PerformanceBenchmarks\n";
    std::cout << "\nOr use CTest:\n";
    std::cout << "  ctest -V\n";
    return 0;
}
#endif
