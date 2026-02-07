# Creating Accurate Mocks

## Overview

Accurate mocks are critical for reliable testing, especially when testing low-level operations like hook attachment and address resolution. This document explains how we create mocks that accurately simulate the game binary.

## Key Principles

### 1. Use Real Prologue Patterns

Mocks must use the **exact same ARM64 prologue patterns** that the game binary uses and that `RuntimeValidation` expects.

```cpp
// ❌ WRONG: Made-up prologue
uint32_t fakePrologue[] = {0x12345678, 0x87654321};

// ✅ CORRECT: Real prologue pattern from RuntimeValidation
ProloguePattern pattern = AccurateMockGenerator::CreateStandardPrologue();
// Creates: STP X29, X30, [SP, #-0x10]! (0xA9800000 pattern)
```

### 2. Validate Against RuntimeValidation

All mock functions are validated using the same `RuntimeValidation::ValidateFunctionPrologue()` that the real hook system uses:

```cpp
void* mockFunc = CreateAccurateMockFunction("TestFunc");
std::string reason;
bool isValid = Platform::RuntimeValidation::ValidateFunctionPrologue(mockFunc, reason);
ASSERT_TRUE(isValid);
```

### 3. Extract Patterns from Real Binary

For maximum accuracy, extract prologue patterns directly from the game binary:

```cpp
// Extract prologue from real function
ProloguePattern realPrologue = AccurateMockGenerator::ExtractPrologueFromSymbol("Main");

// Use extracted pattern for mock
void* mockFunc = AccurateMockGenerator::CreateMockFunctionWithPrologue(realPrologue);
```

## Prologue Patterns

### Standard Patterns (from RuntimeValidation)

The mock generator uses the exact patterns that `RuntimeValidation` recognizes:

1. **STP X29, X30 frame save**
   - Mask: `0xFFC00000`
   - Value: `0xA9800000`
   - Pattern: `STP X29, X30, [SP, #-0x??]!`

2. **SUB SP stack allocation**
   - Mask: `0xFF800000`
   - Value: `0xD1000000`
   - Pattern: `SUB SP, SP, #imm`

3. **MOV X29, SP**
   - Mask: `0xFFE0001F`
   - Value: `0xAA1F03FD`
   - Pattern: `MOV X29, SP`

### Creating Accurate Prologues

```cpp
// Standard prologue (most common)
auto pattern = AccurateMockGenerator::CreateStandardPrologue();
// Creates: STP X29, X30, [SP, #-0x10]!

// Stack allocation prologue
auto pattern = AccurateMockGenerator::CreateStackAllocPrologue(0x20);
// Creates: SUB SP, SP, #0x20

// Frame setup prologue
auto pattern = AccurateMockGenerator::CreateFrameSetupPrologue();
// Creates: MOV X29, SP
```

## Mock Function Creation

### Basic Accurate Mock

```cpp
EnhancedMockGameBinary game;

// Create mock with standard prologue
void* func = game.CreateAccurateMockFunction("MyFunction");

// Validate it passes RuntimeValidation
std::string reason;
ASSERT_TRUE(Platform::RuntimeValidation::ValidateFunctionPrologue(func, reason));
```

### Mock from Real Prologue

```cpp
// Extract prologue from real game function
ProloguePattern realPrologue = AccurateMockGenerator::ExtractPrologueFromSymbol("Main");

// Create mock using real pattern
void* mockFunc = game.CreateMockFunctionWithPattern("MockMain", realPrologue);

// Verify it matches
ASSERT_TRUE(AccurateMockGenerator::ValidateMockPrologue(mockFunc, realPrologue));
```

### Mock from Prologue Database

```cpp
// Load prologue database (extracted from game binary)
PrologueDatabase& db = PrologueDatabase::GetInstance();
db.LoadFromGameBinary("/path/to/game");

// Get prologue for specific hash
ProloguePattern pattern = db.GetPrologueForHash(0x0E54032B); // Main hash

// Create mock
void* mockFunc = game.CreateMockFunctionWithPattern("MockMain", pattern);
```

## Prologue Database

### Building the Database

The prologue database stores patterns extracted from the real game binary:

```cpp
// Analyze game binary and extract common prologues
PrologueDatabase& db = PrologueDatabase::GetInstance();
db.LoadFromGameBinary("/path/to/Cyberpunk2077");

// Save for reuse
db.SaveToFile("prologue_database.json");
```

### Using the Database

```cpp
// Load saved database
db.LoadFromFile("prologue_database.json");

// Get prologue for hash
ProloguePattern pattern = db.GetPrologueForHash(0x0E54032B);

// Get random valid prologue
ProloguePattern randomPattern = db.GetRandomPrologue();
```

## Memory Layout Accuracy

### Address Space Simulation

Mocks simulate the game binary's address space:

```cpp
EnhancedMockGameBinary game;

// Mock uses game-like base address (0x100000000)
void* base = game.GetBaseAddress();
ASSERT_EQ(base, reinterpret_cast<void*>(0x100000000ULL));

// Functions are allocated in executable memory
void* func = game.CreateAccurateMockFunction("Test");
ASSERT_TRUE(IsExecutableMemory(func));
```

### Segment Simulation

Mocks can simulate different memory segments:

```cpp
// __TEXT segment (executable code)
void* textFunc = game.CreateAccurateMockFunction("TextFunction");

// __DATA segment (data)
void* dataPtr = game.CreateDataPointer();

// Validate segment properties
ASSERT_TRUE(IsInTextSegment(textFunc));
ASSERT_TRUE(IsInDataSegment(dataPtr));
```

## Validation Strategy

### Multi-Layer Validation

1. **Pattern Matching**: Verify prologue matches known patterns
2. **RuntimeValidation**: Use same validation as real system
3. **Instruction Decoding**: Verify ARM64 instruction encoding
4. **Memory Properties**: Verify executable memory permissions

```cpp
bool ValidateMockFunction(void* func)
{
    // Layer 1: Pattern matching
    ProloguePattern pattern = ExtractPrologue(func);
    if (!MatchesKnownPattern(pattern))
        return false;

    // Layer 2: RuntimeValidation
    std::string reason;
    if (!Platform::RuntimeValidation::ValidateFunctionPrologue(func, reason))
        return false;

    // Layer 3: Memory properties
    if (!IsExecutableMemory(func))
        return false;

    return true;
}
```

## Testing Mock Accuracy

### Validation Tests

```cpp
TEST(MockAccuracy, StandardPrologue)
{
    EnhancedMockGameBinary game;
    void* func = game.CreateAccurateMockFunction("Test");
    
    // Should pass RuntimeValidation
    std::string reason;
    ASSERT_TRUE(Platform::RuntimeValidation::ValidateFunctionPrologue(func, reason));
    
    // Should match expected pattern
    ProloguePattern expected = AccurateMockGenerator::CreateStandardPrologue();
    ASSERT_TRUE(AccurateMockGenerator::ValidateMockPrologue(func, expected));
}

TEST(MockAccuracy, RealPrologueMatch)
{
    // Extract from real binary
    ProloguePattern real = AccurateMockGenerator::ExtractPrologueFromSymbol("Main");
    
    // Create mock
    EnhancedMockGameBinary game;
    void* mock = game.CreateMockFunctionWithPattern("MockMain", real);
    
    // Should match exactly
    ASSERT_TRUE(AccurateMockGenerator::ValidateMockPrologue(mock, real));
}
```

## Best Practices

### 1. Always Validate

```cpp
// ✅ Always validate mocks
void* func = CreateMockFunction();
ASSERT_TRUE(ValidateMockFunction(func));
```

### 2. Use Real Patterns

```cpp
// ✅ Use patterns from real binary
ProloguePattern pattern = ExtractFromGame("Main");
void* mock = CreateMockWithPattern(pattern);

// ❌ Don't invent patterns
uint32_t fake[] = {0x12345678};
```

### 3. Test Against RuntimeValidation

```cpp
// ✅ Test that mocks pass RuntimeValidation
std::string reason;
ASSERT_TRUE(Platform::RuntimeValidation::ValidateFunctionPrologue(mock, reason));
```

### 4. Reuse Prologue Database

```cpp
// ✅ Load once, reuse many times
PrologueDatabase& db = PrologueDatabase::GetInstance();
db.LoadFromFile("prologue_database.json");

// Use for all tests
ProloguePattern pattern = db.GetPrologueForHash(hash);
```

## Future Enhancements

1. **Automatic Extraction**: Automatically extract all prologues from game binary
2. **Pattern Learning**: Learn new patterns from game updates
3. **Validation Suite**: Comprehensive validation test suite
4. **Performance**: Cache prologues for faster mock creation
5. **Documentation**: Generate documentation from extracted patterns
