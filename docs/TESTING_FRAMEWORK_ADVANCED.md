# Advanced Testing Framework Features

## Runtime Testing

### Testing Against Real Game Binary

Runtime tests execute against the actual Cyberpunk 2077 binary, providing the most realistic test environment.

```cpp
#include "tests/lib/RuntimeTestFramework.hpp"

RUNTIME_TEST(ResolveMainFunction)
{
    auto game = GameBinaryInterface::Create("/path/to/game");
    void* mainAddr = game->GetFunctionByHash(0x0E54032B);
    return mainAddr != nullptr && game->IsValidGameAddress(mainAddr);
}
END_RUNTIME_TEST
```

### Running Runtime Tests

```bash
RED4EXT_GAME_PATH=/path/to/game ./RuntimeTests
```

## Performance Benchmarking

### Benchmark Framework

Measure performance of critical operations:

```cpp
#include "tests/lib/PerformanceBenchmark.hpp"

BENCHMARK(HookAttachmentSpeed, 1000)
{
    MockGameBinary game;
    void* target = game.CreateMockFunction("Test");
    void* detour = TestFunction_Simple;
    
    void* trampoline = target;
    NativeDetourAttach(&trampoline, detour);
    NativeDetourDetach(&trampoline, detour);
}
END_BENCHMARK
```

### Benchmark Results

Benchmarks provide:
- Min/Max/Average/Median execution time
- Statistical analysis
- Comparison with previous runs
- Performance regression detection

## Test Reporting

### Multiple Report Formats

Generate reports in various formats:

```cpp
// JSON report
TestReporter::GenerateReport(suites, ReportFormat::JSON, "report.json");

// HTML report
TestReporter::GenerateReport(suites, ReportFormat::HTML, "report.html");

// JUnit XML (for CI)
TestReporter::GenerateReport(suites, ReportFormat::JUnit, "junit.xml");
```

### Report Features

- Test summary statistics
- Failure details with error messages
- Execution time per test
- Historical trend tracking
- Coverage information

## Property-Based Testing

### Generate Test Cases Automatically

```cpp
PROPERTY_TEST(AddressResolutionProperty)
{
    // Generate random valid hashes
    for (int i = 0; i < 1000; ++i)
    {
        uint32_t hash = GenerateRandomHash();
        auto addr = addresses.Resolve(hash);
        
        // Property: Resolution should never crash
        // Property: If non-zero, address should be valid
        if (addr != 0)
        {
            ASSERT_TRUE(IsValidAddress(addr));
        }
    }
}
```

## Mutation Testing

### Verify Test Quality

Mutation testing modifies code slightly and verifies tests catch the changes:

```cpp
MUTATION_TEST(AddressResolutionMutation)
{
    // Original: addresses.Resolve(hash)
    // Mutation: addresses.Resolve(hash + 1)
    // Test should fail if mutation is applied
}
```

## Continuous Testing

### Run Tests on File Changes

```bash
# Watch for changes and run tests
watchmedo shell-command \
    --patterns="*.cpp;*.hpp" \
    --recursive \
    --command='cd build-macos && ctest'
```

## Test Parallelization

### Run Tests in Parallel

```cpp
// Run tests in parallel threads
TestRunner runner;
runner.SetParallel(true);
runner.SetThreadCount(4);
runner.RunAll();
```

## Coverage Analysis

### Track Code Coverage

```bash
# Build with coverage
cmake .. -DCMAKE_BUILD_TYPE=Coverage
make

# Run tests
ctest

# Generate coverage report
gcov src/dll/*.cpp
lcov --capture --directory . --output-file coverage.info
genhtml coverage.info --output-directory coverage_html
```

## Visual Test Reports

### HTML Dashboards

Generate interactive HTML dashboards showing:
- Test results over time
- Coverage trends
- Performance benchmarks
- Failure analysis

## Test Fixtures

### Reusable Test Setup

```cpp
class AddressResolutionFixture
{
public:
    AddressResolutionFixture()
    {
        // Set up test address database
        CreateTestDatabase();
        addresses.LoadAddresses("test_db.json");
    }
    
    ~AddressResolutionFixture()
    {
        // Cleanup
        RemoveTestDatabase();
    }
    
    Addresses addresses;
};

TEST(ResolveHash, WithFixture)
{
    AddressResolutionFixture fixture;
    auto addr = fixture.addresses.Resolve(0x12345678);
    ASSERT_NE(addr, 0);
}
```

## Mock Framework

### Advanced Mocking

```cpp
// Mock with expectations
MockHookTarget mock;
mock.ExpectCall("MyFunction", 3); // Expect 3 calls
mock.SetReturnValue("MyFunction", 42);

// Verify expectations
ASSERT_TRUE(mock.VerifyExpectations());
```

## Fuzzing Framework

### Automated Fuzzing

```cpp
FUZZ_TEST(AddressHashFuzzing)
{
    // Generate random inputs
    uint32_t hash = Fuzzer::GenerateRandom<uint32_t>();
    
    // Test should handle gracefully
    auto addr = addresses.Resolve(hash);
    // Should not crash
}
```

## Integration with CI/CD

### GitHub Actions

```yaml
name: Tests
on: [push, pull_request]
jobs:
  test:
    runs-on: macos-latest
    steps:
      - uses: actions/checkout@v2
      - name: Build
        run: |
          mkdir build && cd build
          cmake ..
          make
      - name: Test
        run: |
          cd build
          ctest --output-on-failure
      - name: Coverage
        run: |
          lcov --capture --directory . --output-file coverage.info
      - name: Upload Coverage
        uses: codecov/codecov@v2
```

## Test Data Management

### Test Data Generators

```cpp
class TestDataGenerator
{
public:
    static std::vector<uint32_t> GenerateValidHashes(size_t count);
    static std::vector<void*> GenerateValidAddresses(size_t count);
    static std::string GenerateRandomPluginName();
};
```

## Best Practices

1. **Fast Tests**: Unit tests should run in <1s
2. **Isolated Tests**: Each test should be independent
3. **Deterministic**: Tests should produce consistent results
4. **Clear Names**: Test names should describe what they test
5. **Good Coverage**: Test both success and failure paths
6. **Maintainable**: Tests should be easy to update
7. **Documented**: Complex tests should have comments

## Future Enhancements

1. **AI-Powered Test Generation**: Generate tests automatically
2. **Visual Test Debugging**: Step through test execution
3. **Test Impact Analysis**: Run only affected tests
4. **Performance Profiling**: Identify slow tests
5. **Test Flakiness Detection**: Identify unstable tests
6. **Cross-Platform Testing**: Test on multiple macOS versions
7. **Regression Test Suite**: Track regressions over time
