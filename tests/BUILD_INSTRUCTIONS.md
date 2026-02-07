# Building and Running Tests

## Prerequisites

- macOS with ARM64 support
- CMake 3.23 or later
- Clang compiler
- RED4ext built (for linking)

## Building Tests

### Step 1: Configure CMake with Tests Enabled

```bash
cd build-macos
cmake .. -DRED4EXT_ENABLE_TESTS=ON
```

### Step 2: Build Tests

```bash
make
```

This will build:
- `TestFramework` library
- `UnitTests_Addresses` executable
- `UnitTests_Validation` executable
- `IntegrationTests` executable
- `PerformanceBenchmarks` executable

## Running Tests

### Option 1: Use CTest (Recommended)

```bash
cd build-macos
ctest -V
```

Run specific test:
```bash
ctest -R UnitTests_Addresses -V
```

### Option 2: Use Test Runner Script

```bash
./tests/run_tests.sh build-macos all
./tests/run_tests.sh build-macos unit
./tests/run_tests.sh build-macos integration
./tests/run_tests.sh build-macos performance
```

### Option 3: Run Individual Executables

```bash
cd build-macos
./tests/UnitTests_Addresses
./tests/UnitTests_Validation
./tests/IntegrationTests
./tests/PerformanceBenchmarks
```

## Test Output

Tests output results in the format:
```
Running test suite: AddressResolution
  [PASS] ResolveValidHash (5ms)
  [PASS] ResolveInvalidHash (2ms)
  [FAIL] LoadAddressDatabase (10ms) - Expected: 1, Actual: 0

================================
Total: 3 tests
Passed: 2
Failed: 1
```

## Generating Reports

### JSON Report

```bash
RED4EXT_TEST_REPORT_FORMAT=json RED4EXT_TEST_REPORT_PATH=report.json ./tests/UnitTests_Addresses
```

### HTML Report

```bash
RED4EXT_TEST_REPORT_FORMAT=html RED4EXT_TEST_REPORT_PATH=report.html ./tests/UnitTests_Addresses
```

### JUnit XML (for CI)

```bash
RED4EXT_TEST_REPORT_FORMAT=junit RED4EXT_TEST_REPORT_PATH=junit.xml ./tests/UnitTests_Addresses
```

## Troubleshooting

### Tests Don't Build

1. Ensure `RED4EXT_ENABLE_TESTS=ON` is set
2. Check that RED4ext.Dll is built first
3. Verify all dependencies are available

### Tests Fail to Run

1. Check that executables are in the correct location
2. Verify macOS platform (tests are macOS-only)
3. Check for missing shared libraries

### Mock Functions Fail Validation

1. Ensure AccurateMockGenerator is linked
2. Check that RuntimeValidation is available
3. Verify prologue patterns match RuntimeValidation expectations

## CI/CD Integration

### GitHub Actions Example

```yaml
- name: Build Tests
  run: |
    cd build-macos
    cmake .. -DRED4EXT_ENABLE_TESTS=ON
    make

- name: Run Tests
  run: |
    cd build-macos
    ctest --output-on-failure

- name: Upload Test Results
  if: always()
  uses: actions/upload-artifact@v2
  with:
    name: test-results
    path: build-macos/test_report.xml
```
