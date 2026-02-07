cmake_minimum_required(VERSION 3.23)

if(APPLE)
    # Test framework library
    add_library(TestFramework STATIC
        lib/TestFramework.hpp
        fixtures/MockGameBinary.cpp
        fixtures/AccurateMockGenerator.cpp
        fixtures/EnhancedMockGameBinary.cpp
    )

    target_include_directories(TestFramework PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/src/dll
    )

    target_link_libraries(TestFramework
        ${CMAKE_DL_LIBS}
    )

    # Link against RED4ext.Dll for RuntimeValidation
    target_link_libraries(TestFramework
        RED4ext.Dll
    )

    # Unit tests - individual executables
    add_executable(UnitTests_Addresses
        unit/Addresses/AddressResolutionTests.cpp
    )

    target_link_libraries(UnitTests_Addresses
        TestFramework
        RED4ext.Dll
    )

    target_include_directories(UnitTests_Addresses PRIVATE
        ${CMAKE_SOURCE_DIR}/src/dll
    )

    add_test(NAME UnitTests_Addresses COMMAND UnitTests_Addresses)

    add_executable(UnitTests_Validation
        unit/RuntimeValidation/ValidationTests.cpp
    )

    target_link_libraries(UnitTests_Validation
        TestFramework
        RED4ext.Dll
    )

    target_include_directories(UnitTests_Validation PRIVATE
        ${CMAKE_SOURCE_DIR}/src/dll
    )

    add_test(NAME UnitTests_Validation COMMAND UnitTests_Validation)

    # Note: Individual test executables are created above
    # Use CTest to run all tests together

    # Integration tests
    add_executable(IntegrationTests
        integration/HookAttachment/HookTests.cpp
    )

    target_link_libraries(IntegrationTests
        TestFramework
        RED4ext.Dll
    )

    target_include_directories(IntegrationTests PRIVATE
        ${CMAKE_SOURCE_DIR}/src/dll
    )

    add_test(NAME IntegrationTests COMMAND IntegrationTests)

    # Performance benchmarks
    add_executable(PerformanceBenchmarks
        performance/HookOverheadBenchmark.cpp
    )

    target_link_libraries(PerformanceBenchmarks
        TestFramework
        RED4ext.Dll
    )

    target_include_directories(PerformanceBenchmarks PRIVATE
        ${CMAKE_SOURCE_DIR}/src/dll
    )

    add_test(NAME PerformanceBenchmarks COMMAND PerformanceBenchmarks)

    # Enable testing
    enable_testing()
endif()
