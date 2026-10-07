cmake_minimum_required(VERSION 3.26)

# ------------------------------------------------------------------------------
# Project variables
# ------------------------------------------------------------------------------
set(RED4EXT_CMAKE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
set(RED4EXT_CMAKE_TEMPLATES_DIR "${RED4EXT_CMAKE_DIR}/templates")

list(APPEND CMAKE_MODULE_PATH "${RED4EXT_CMAKE_DIR}")

# ------------------------------------------------------------------------------
# Modules
# ------------------------------------------------------------------------------
include("${RED4EXT_CMAKE_DIR}/ConfigureDefaultOutputDirectories.cmake")
include("${RED4EXT_CMAKE_DIR}/ConfigureResourceRc.cmake")
include("${RED4EXT_CMAKE_DIR}/ConfigureVersionFile.cmake")
include("${RED4EXT_CMAKE_DIR}/ConfigureVersionFromGit.cmake")
include("${RED4EXT_CMAKE_DIR}/TargetAppendOutputDirectory.cmake")

# ------------------------------------------------------------------------------
# Versioning
# ------------------------------------------------------------------------------
red4ext_configure_version_from_git()

# ------------------------------------------------------------------------------
# Project definition
# ------------------------------------------------------------------------------
project(
  RED4ext
  VERSION "${RED4EXT_GIT_VERSION_MAJOR}.${RED4EXT_GIT_VERSION_MINOR}.${RED4EXT_GIT_VERSION_PATCH}"
  LANGUAGES CXX C
)

# ------------------------------------------------------------------------------
# General configuration
# ------------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
  set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
  set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_MINSIZEREL ON)
  set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO ON)
  set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "ProgramDatabase")
endif()

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

red4ext_configure_default_output_directories()

if(APPLE)
  # tools/cp-dev, scripts/create_release.sh and scripts/macos_install.sh pick RED4ext.dylib up from libs/.
  set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/libs")
  set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/libs")
endif()

# ------------------------------------------------------------------------------
# Options
# ------------------------------------------------------------------------------
option(RED4EXT_GIT_METADATA "Append Git metadata to 'RED4EXT_GIT_VERSION' CMake variable." ON)

option(RED4EXT_EXTRA_WARNINGS "Enable extra warnings." ON)
option(RED4EXT_TREAT_WARNINGS_AS_ERRORS "Treat compiler warnings as errors." OFF)

# ------------------------------------------------------------------------------
# Toolchain settings for all targets
# ------------------------------------------------------------------------------
if(WIN32)
  add_compile_definitions(
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN

    # Support Windows 7 and above.
    WINVER=0x0601
    _WIN32_WINNT=0x0601
  )
elseif(APPLE)
  add_compile_definitions(RED4EXT_PLATFORM_MACOS)

  # Ensure ARM64 on Apple Silicon if not specified
  if(NOT CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES "arm64")
  endif()
endif()

if(MSVC)
  add_link_options(
    /DEBUG:FULL

    $<$<CONFIG:Release,MinSizeRel,RelWithDebInfo>:/INCREMENTAL:NO>
  )
endif()

# ------------------------------------------------------------------------------
# Dependencies (git submodules in deps/, see .gitmodules)
# ------------------------------------------------------------------------------
include(ConfigureAndIncludeFmt)
include(ConfigureAndIncludeRED4extSdk)
include(ConfigureAndIncludeSimdjson)
include(ConfigureAndIncludeSpdlog)
include(ConfigureAndIncludeToml11)
include(ConfigureAndIncludeTslOrderedMap)
include(ConfigureAndIncludeRedscript)

if(WIN32)
  include(ConfigureAndIncludeDetours)
  include(ConfigureAndIncludeWil)
elseif(APPLE)
  include(ConfigureAndIncludeFishhook)
endif()

# ------------------------------------------------------------------------------
# Toolchain settings for this project only
# ------------------------------------------------------------------------------
if(MSVC)
  add_compile_definitions(
    NOMINMAX
  )

  add_compile_options(
    /GS
    /sdl

    /permissive-
    /Zc:__cplusplus
    /Zc:__STDC__
    /Zc:enumTypes
    /Zc:inline
    /Zc:preprocessor
    /Zc:templateScope

    $<$<CONFIG:Release,MinSizeRel,RelWithDebInfo>:/Gy>
    $<$<CONFIG:Release,MinSizeRel,RelWithDebInfo>:/Oi>

    # Disable some warnings that are not relevant to us.
    /external:I${PROJECT_SOURCE_DIR}/deps
    /external:W0
  )
endif()

if(RED4EXT_EXTRA_WARNINGS)
  if(MSVC)
    add_compile_options(/W4)
  else()
    add_compile_options(-Wall -Wextra)
  endif()
endif()

if(RED4EXT_TREAT_WARNINGS_AS_ERRORS)
  if(MSVC)
    add_compile_options(/WX)
  else()
    add_compile_options(-Werror)
  endif()
endif()

# ------------------------------------------------------------------------------
# Main projects
# ------------------------------------------------------------------------------
add_subdirectory(src)

# Build validation plugin and tests on macOS
if(APPLE)
  enable_testing()
  add_subdirectory(tests)
endif()

# ------------------------------------------------------------------------------
# Install
# ------------------------------------------------------------------------------
if(WIN32 AND NOT CMAKE_SKIP_INSTALL_RULES)
  set(RED4EXT_INSTALL_X64_DIR bin/x64)
  set(RED4EXT_INSTALL_BIN_DIR red4ext)

  install(
    TARGETS RED4ext.Loader
    RUNTIME DESTINATION "${RED4EXT_INSTALL_X64_DIR}"
  )

  install(
    FILES $<TARGET_PDB_FILE:RED4ext.Loader>
    DESTINATION "${RED4EXT_INSTALL_X64_DIR}"
  )

  install(
    TARGETS RED4ext.Dll
    RUNTIME DESTINATION "${RED4EXT_INSTALL_BIN_DIR}"
  )

  install(
    FILES $<TARGET_PDB_FILE:RED4ext.Dll>
    DESTINATION "${RED4EXT_INSTALL_BIN_DIR}"
  )

  install(
    FILES "${PROJECT_SOURCE_DIR}/LICENSE.md"
    DESTINATION "${RED4EXT_INSTALL_BIN_DIR}"
    RENAME LICENSE.txt
  )

  install(
    FILES "${PROJECT_SOURCE_DIR}/THIRD_PARTY_LICENSES.md"
    DESTINATION "${RED4EXT_INSTALL_BIN_DIR}"
    RENAME THIRD_PARTY_LICENSES.txt
  )
endif()
