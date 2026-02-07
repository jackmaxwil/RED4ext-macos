# RED4ext Testing Framework

## Architecture

### Test Categories

1. **Unit Tests**: Test individual components in isolation
2. **Integration Tests**: Test component interactions
3. **Runtime Tests**: Test against actual game binary
4. **Performance Tests**: Benchmark critical paths
5. **Fuzzing Tests**: Test with invalid/random inputs

### Test Framework Components

#### TestFramework Library (`tests/lib/TestFramework.hpp`)
- Test runner with suite/test organization
- Assertions with detailed error messages
- Test result reporting
- Timing and statistics

#### Mock Framework (`tests/fixtures/`)
- **MockGameBinary**: Simulates game executable
- **MockPlugin**: Simulates plugin for testing
- **MockHookTarget**: Provides hookable functions
- **TestHelpers**: Common test utilities

## Writing Tests

### Unit Test Example

```cpp
#include "tests/lib/TestFramework.hpp"
#include "Addresses.hpp"

TEST_SUITE(AddressResolution)
{
    TEST(ResolveValidHash)
    {
        Paths paths("/tmp");
        Addresses addresses(paths);
        
        // Load test database
        addresses.LoadAddresses("test_addresses.json");
        
        // Test resolution
        auto addr = addresses.Resolve(0x0E54032B);
        ASSERT_NE(addr, 0);
    }
}
```

### Integration Test Example

```cpp
#include "tests/lib/TestFramework.hpp"
#include "tests/fixtures/MockGameBinary.hpp"
#include "Platform/Hooking.hpp"

TEST_SUITE(HookAttachment)
{
    TEST(AttachDetachHook)
    {
        MockGameBinary game;
        void* target = game.CreateMockFunction("TestFunction");
        
        bool called = false;
        auto detour = [&]() { called = true; };
        
        void* original = target;
        ASSERT_EQ(NativeDetourAttach(&target, detour), NO_ERROR);
        ASSERT_NE(target, original);
        
        // Call trampoline
        target();
        
        // Detach
        ASSERT_EQ(NativeDetourDetach(&target, detour), NO_ERROR);
    }
}
```

## Test Execution

### Running All Tests
```bash
cd build-macos
ctest -V
```

### Running Specific Suite
```bash
ctest -R UnitTests -V
```

### Running Individual Test
```bash
./build-macos/tests/UnitTests --gtest_filter=AddressResolution.ResolveValidHash
```

## Mock Components

### MockGameBinary

Creates mock functions with valid ARM64 prologues for testing hook attachment:

```cpp
MockGameBinary game;
void* func = game.CreateMockFunction("MyFunction");
game.RegisterFunction(0x12345678, "MyFunction", func);
```

### MockPlugin

Simulates plugin loading/unloading:

```cpp
MockPlugin plugin("TestPlugin", "1.0.0");
ASSERT_TRUE(plugin.Load());
ASSERT_TRUE(plugin.Initialize());
plugin.Unload();
```

## Test Fixtures

### Common Fixtures

- **TestAddressDatabase**: Pre-populated address database
- **TestConfig**: Test configuration
- **TestPaths**: Test directory structure
- **TestLogger**: Captures log output

## Assertions

### Basic Assertions
- `ASSERT(condition)` - Generic assertion
- `ASSERT_TRUE(condition)` - Assert true
- `ASSERT_FALSE(condition)` - Assert false
- `ASSERT_EQ(expected, actual)` - Assert equality
- `ASSERT_NE(expected, actual)` - Assert inequality
- `ASSERT_NULL(ptr)` - Assert null pointer
- `ASSERT_NOT_NULL(ptr)` - Assert non-null pointer

### Specialized Assertions
- `ASSERT_VALID_ADDRESS(addr)` - Assert valid memory address
- `ASSERT_VALID_PROLOGUE(addr)` - Assert valid function prologue
- `ASSERT_HOOK_ATTACHED(target)` - Assert hook is attached

## Test Organization

### Directory Structure
```
tests/
├── unit/              # Unit tests
│   ├── Addresses/
│   ├── Hooking/
│   └── PluginSystem/
├── integration/       # Integration tests
│   ├── HookAttachment/
│   └── PluginLoading/
├── runtime/           # Runtime tests
│   └── GameBinary/
├── performance/       # Performance benchmarks
└── fixtures/          # Test fixtures
```

## CI/CD Integration

### GitHub Actions

Tests run on:
- Pull requests
- Commits to main
- Nightly builds

### Test Reports

- JUnit XML for CI integration
- HTML reports for local viewing
- JSON reports for programmatic analysis

## Coverage

### Coverage Goals
- Unit tests: >80% code coverage
- Integration tests: All critical paths
- Runtime tests: All hook types

### Coverage Tools
- Use `gcov` for coverage analysis
- Generate HTML reports with `lcov`
- Track coverage trends over time

## Performance Testing

### Benchmark Framework

```cpp
BENCHMARK(HookOverhead)
{
    MockGameBinary game;
    void* target = game.CreateMockFunction("BenchFunction");
    
    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < 1000000; ++i)
    {
        target(); // Call hooked function
    }
    auto end = std::chrono::high_resolution_clock::now();
    
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    BENCHMARK_REPORT("Hook call overhead", duration.count() / 1000000.0, "us");
}
```

## Fuzzing

### Address Hash Fuzzing

```cpp
FUZZ_TEST(AddressHashFuzzing)
{
    uint32_t hash = GetRandomHash();
    Addresses addresses;
    auto addr = addresses.Resolve(hash);
    // Should not crash, may return 0
    ASSERT_TRUE(addr == 0 || IsValidAddress(addr));
}
```

## Best Practices

1. **Isolation**: Each test should be independent
2. **Determinism**: Tests should produce consistent results
3. **Speed**: Unit tests should run quickly (<1s each)
4. **Clarity**: Test names should describe what they test
5. **Coverage**: Test both success and failure paths
6. **Mocking**: Use mocks to isolate components
7. **Fixtures**: Reuse common test setup

## Future Enhancements

1. **Property-Based Testing**: Generate test cases automatically
2. **Mutation Testing**: Verify test quality
3. **Visual Test Reports**: HTML dashboards
4. **Test Parallelization**: Run tests in parallel
5. **Continuous Testing**: Run tests on file changes
6. **Test Coverage Visualization**: See what's covered
7. **Performance Regression Detection**: Track performance over time
