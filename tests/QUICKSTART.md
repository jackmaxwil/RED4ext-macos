# Testing Framework Quick Start

## Overview

RED4ext testing framework provides comprehensive testing capabilities for unit tests, integration tests, runtime tests, and performance benchmarks.

## Quick Start

### 1. Build Tests

```bash
cd build-macos
cmake .. -DENABLE_TESTS=ON
make
```

### 2. Run All Tests

```bash
cd build-macos
ctest -V
```

### 3. Run Specific Test Suite

```bash
./build-macos/tests/UnitTests
./build-macos/tests/IntegrationTests
```

### 4. Run Performance Benchmarks

```bash
./build-macos/tests/PerformanceBenchmarks
```

## Writing Your First Test

### Unit Test Example

```cpp
#include "tests/lib/TestFramework.hpp"
#include "Addresses.hpp"

TEST_SUITE(MyTestSuite)
{
    TEST(MyFirstTest)
    {
        // Your test code here
        ASSERT_TRUE(true);
    }
    
    TEST(MySecondTest)
    {
        int value = 42;
        ASSERT_EQ(value, 42);
    }
}
END_TEST_SUITE

int main()
{
    TestFramework::TestRunner::GetInstance().RegisterSuite("MyTestSuite", RegisterSuite_MyTestSuite);
    return TestFramework::TestRunner::GetInstance().RunAll();
}
```

### Integration Test Example

```cpp
#include "tests/lib/TestFramework.hpp"
#include "tests/fixtures/MockGameBinary.hpp"
#include "Platform/Hooking.hpp"

TEST_SUITE(HookTests)
{
    TEST(AttachHook)
    {
        MockGameBinary game;
        void* target = game.CreateMockFunction("TestFunc");
        
        bool called = false;
        auto detour = [&]() { called = true; };
        
        void* original = target;
        ASSERT_EQ(NativeDetourAttach(&target, detour), NO_ERROR);
        ASSERT_NE(target, original);
    }
}
END_TEST_SUITE
```

## Test Categories

### Unit Tests (`tests/unit/`)
- Fast, isolated tests
- No external dependencies
- Test individual components

### Integration Tests (`tests/integration/`)
- Test component interactions
- Use mocks for external dependencies
- Test real workflows

### Runtime Tests (`tests/runtime/`)
- Test against actual game binary
- Most realistic test environment
- Require game installation

### Performance Tests (`tests/performance/`)
- Benchmark critical operations
- Track performance over time
- Detect regressions

## Assertions

### Basic Assertions
```cpp
ASSERT(condition);
ASSERT_TRUE(condition);
ASSERT_FALSE(condition);
ASSERT_EQ(expected, actual);
ASSERT_NE(expected, actual);
ASSERT_NULL(ptr);
ASSERT_NOT_NULL(ptr);
```

### Specialized Assertions
```cpp
ASSERT_VALID_ADDRESS(addr);
ASSERT_VALID_PROLOGUE(addr);
ASSERT_HOOK_ATTACHED(target);
```

## Mock Components

### MockGameBinary
```cpp
MockGameBinary game;
void* func = game.CreateMockFunction("MyFunction");
game.RegisterFunction(0x12345678, "MyFunction", func);
```

### MockPlugin
```cpp
MockPlugin plugin("TestPlugin", "1.0.0");
ASSERT_TRUE(plugin.Load());
```

## Test Reports

### Generate JSON Report
```cpp
TestReporter::GenerateReport(suites, ReportFormat::JSON, "report.json");
```

### Generate HTML Report
```cpp
TestReporter::GenerateReport(suites, ReportFormat::HTML, "report.html");
```

## Performance Benchmarking

### Write Benchmark
```cpp
BENCHMARK(MyBenchmark, 1000)
{
    // Code to benchmark
}
END_BENCHMARK
```

### Run Benchmarks
```cpp
auto& runner = BenchmarkRunner::GetInstance();
auto results = runner.RunAll();
runner.PrintResults(results);
```

## Best Practices

1. **Keep tests fast**: Unit tests should run in <1s
2. **Isolate tests**: Each test should be independent
3. **Use descriptive names**: Test names should describe what they test
4. **Test both paths**: Test success and failure cases
5. **Use mocks**: Mock external dependencies
6. **Clean up**: Clean up resources in test teardown

## Next Steps

- Read [TESTING_FRAMEWORK.md](../docs/TESTING_FRAMEWORK.md) for detailed documentation
- Read [TESTING_FRAMEWORK_ADVANCED.md](../docs/TESTING_FRAMEWORK_ADVANCED.md) for advanced features
- Check existing tests in `tests/unit/` and `tests/integration/` for examples
