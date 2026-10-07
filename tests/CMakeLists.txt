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

    add_library(native_hook_plugin_stub SHARED native_hook/plugin_stub.c)
    set_target_properties(native_hook_plugin_stub PROPERTIES
        PREFIX "lib"
        OUTPUT_NAME "native_hook_plugin_stub"
    )

    add_executable(native_hook_tests
        native_hook/main.cpp
        ${CMAKE_SOURCE_DIR}/src/dll/Platform/NativeHook.cpp
        ${CMAKE_SOURCE_DIR}/src/dll/Platform/MainLookup.cpp
    )
    target_include_directories(native_hook_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/dll)
    target_compile_definitions(native_hook_tests PRIVATE RED4EXT_PLATFORM_MACOS RED4EXT_NATIVE_HOOK_TEST)
    target_link_libraries(native_hook_tests PRIVATE ${CMAKE_DL_LIBS})
    add_dependencies(native_hook_tests native_hook_plugin_stub)

    add_custom_command(TARGET native_hook_tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:native_hook_plugin_stub> $<TARGET_FILE_DIR:native_hook_tests>
        COMMAND /bin/sh ${CMAKE_SOURCE_DIR}/scripts/codesign_macos.sh dylib $<TARGET_FILE_DIR:native_hook_tests>/$<TARGET_FILE_NAME:native_hook_plugin_stub>
        COMMAND /bin/sh ${CMAKE_SOURCE_DIR}/scripts/codesign_macos.sh exe $<TARGET_FILE:native_hook_tests>
        COMMAND /bin/sh ${CMAKE_SOURCE_DIR}/scripts/codesign_macos.sh verify $<TARGET_FILE:native_hook_tests>
    )

    add_test(NAME native_hook_tests COMMAND native_hook_tests)

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

    # Address validation plugin (runtime, loaded by RED4ext)
    add_library(AddressValidator SHARED
        validation_plugin/Main.cpp
        validation_plugin/AddressHashList.hpp
    )

    target_include_directories(AddressValidator PRIVATE
        ${CMAKE_SOURCE_DIR}/deps/red4ext.sdk/include
        ${CMAKE_SOURCE_DIR}/deps/spdlog/include
        ${CMAKE_SOURCE_DIR}/deps/fmt/include
    )

    target_link_libraries(AddressValidator
        spdlog::spdlog
        fmt::fmt
    )

    set_target_properties(AddressValidator PROPERTIES
        OUTPUT_NAME "address_validator"
        PREFIX ""
        SUFFIX ".dylib"
    )

    # Enable testing
    enable_testing()
endif()
