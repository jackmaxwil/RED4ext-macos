# RED4ext Testing Framework

## Overview

Comprehensive testing framework for RED4ext macOS port, covering unit tests, integration tests, runtime validation, and performance benchmarks.

## Test Categories

### 1. Unit Tests (`tests/unit/`)
- **Address Resolution**: Test address database loading, hash resolution, symbol mapping
- **Hook Validation**: Test prologue validation, address range checks, branch island detection
- **Plugin System**: Test plugin loading, version checking, API compatibility
- **Configuration**: Test config parsing, path resolution, environment variable handling
- **Utilities**: Test string conversion, path manipulation, error handling

### 2. Integration Tests (`tests/integration/`)
- **Hook Attachment**: Test hook attachment/detachment, trampoline creation
- **Plugin Loading**: Test plugin discovery, loading, initialization, unloading
- **Address Resolution**: Test end-to-end address resolution with real game binary
- **System Integration**: Test system startup/shutdown, state transitions

### 3. Runtime Tests (`tests/runtime/`)
- **Game Binary Tests**: Tests that run against actual Cyberpunk 2077 binary
- **Hook Execution**: Verify hooks are called correctly
- **Memory Validation**: Test memory protection, address validation
- **Crash Recovery**: Test crash handling, signal handlers

### 4. Performance Tests (`tests/performance/`)
- **Hook Overhead**: Measure hook call overhead
- **Address Resolution Speed**: Benchmark address resolution performance
- **Plugin Load Time**: Measure plugin loading performance
- **Memory Usage**: Track memory consumption

### 5. Fuzzing Tests (`tests/fuzzing/`)
- **Address Hash Fuzzing**: Test with random/invalid hashes
- **Hook Target Fuzzing**: Test with invalid addresses
- **Config Fuzzing**: Test with malformed configs

## Test Framework Components

### Test Infrastructure
- **Test Runner**: Custom test runner with macOS-specific features
- **Mock Framework**: Mock game binary, hooks, plugins
- **Fixtures**: Reusable test fixtures for common scenarios
- **Assertions**: Enhanced assertions with context
- **Test Reports**: JSON/HTML test reports

### Mock Components
- **MockGameBinary**: Simulates game executable
- **MockPlugin**: Simulates plugin for testing
- **MockHookTarget**: Provides hookable functions
- **MockAddressDatabase**: Provides address database for testing

## Running Tests

### Unit Tests (No Game Required)
```bash
cd build-macos
ctest -R Unit -V
```

### Integration Tests (Requires Mock Game)
```bash
cd build-macos
ctest -R Integration -V
```

### Runtime Tests (Requires Game Binary)
```bash
cd build-macos
RED4EXT_GAME_PATH=/path/to/game ctest -R Runtime -V
```

### All Tests
```bash
cd build-macos
ctest -V
```

## Test Structure

```
tests/
├── CMakeLists.txt          # Test build configuration
├── unit/                   # Unit tests
│   ├── Addresses/
│   ├── Hooking/
│   ├── PluginSystem/
│   └── Utils/
├── integration/            # Integration tests
│   ├── HookAttachment/
│   ├── PluginLoading/
│   └── SystemStartup/
├── runtime/                # Runtime tests
│   ├── GameBinary/
│   ├── HookExecution/
│   └── CrashRecovery/
├── performance/            # Performance benchmarks
│   ├── HookOverhead/
│   └── AddressResolution/
├── fuzzing/                # Fuzzing tests
│   └── AddressHash/
├── fixtures/               # Test fixtures
│   ├── MockGameBinary.hpp
│   ├── MockPlugin.hpp
│   └── TestHelpers.hpp
└── lib/                    # Test utilities
    ├── TestRunner.hpp
    ├── Assertions.hpp
    └── MockFramework.hpp
```

## Writing Tests

### Example Unit Test
```cpp
#include "tests/fixtures/TestHelpers.hpp"
#include "Addresses.hpp"

TEST(Addresses, ResolveValidHash)
{
    auto addresses = CreateTestAddressDatabase();
    auto addr = addresses->Resolve(0x0E54032B); // Main hash
    ASSERT_NE(addr, 0);
    ASSERT_TRUE(IsValidAddress(addr));
}
```

### Example Integration Test
```cpp
#include "tests/fixtures/MockGameBinary.hpp"
#include "Platform/Hooking.hpp"

TEST(HookAttachment, AttachDetach)
{
    MockGameBinary game;
    void* target = game.GetFunction("TestFunction");
    
    bool called = false;
    auto detour = [&]() { called = true; };
    
    void* trampoline = nullptr;
    ASSERT_EQ(NativeDetourAttach(&target, detour), NO_ERROR);
    ASSERT_NE(target, nullptr);
    
    // Call trampoline
    target();
    ASSERT_TRUE(called);
    
    // Detach
    ASSERT_EQ(NativeDetourDetach(&target, detour), NO_ERROR);
}
```

## CI/CD Integration

Tests run automatically on:
- Pull requests
- Commits to main/master
- Nightly builds

Test results are published as:
- GitHub Actions annotations
- Test reports (JSON/HTML)
- Coverage reports

## Coverage Goals

- **Unit Tests**: >80% code coverage
- **Integration Tests**: All critical paths
- **Runtime Tests**: All hook types, plugin scenarios
- **Performance Tests**: All performance-critical paths
