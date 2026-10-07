add_library(RED4ext.Dll SHARED)

set_target_properties(
  RED4ext.Dll
    PROPERTIES
      OUTPUT_NAME ${PROJECT_NAME}
)

if(APPLE)
  set_target_properties(
    RED4ext.Dll
      PROPERTIES
        PREFIX ""
        SUFFIX ".dylib"
  )
endif()

if(WIN32)
  red4ext_configure_resource_rc(
    RED4ext.Dll
    "A script extender for REDengine 4."
  )
endif()

red4ext_configure_version_file(RED4ext.Dll)

file(
  GLOB_RECURSE
    RED4EXT_FILES
      *.hpp
      *.cpp
)

source_group(CMake REGULAR_EXPRESSION cmake_pch.*)

source_group(
  TREE
    "${CMAKE_CURRENT_SOURCE_DIR}"
  FILES
    ${RED4EXT_FILES}
)

source_group(
  TREE
    "${CMAKE_CURRENT_BINARY_DIR}"
  FILES
    ${RED4EXT_RC_FILE}
    ${RED4EXT_VERSION_FILE}
)

# Public: the tests link against RED4ext.Dll and use its headers and precompiled header.
target_include_directories(
  RED4ext.Dll
    PUBLIC
      "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_precompile_headers(
  RED4ext.Dll
    PUBLIC
      stdafx.hpp
)

target_sources(
  RED4ext.Dll
    PRIVATE
      ${RED4EXT_FILES}
      ${RED4EXT_RC_FILE}
      ${RED4EXT_VERSION_FILE}
)

if(WIN32)
  target_link_libraries(
    RED4ext.Dll
      PRIVATE
        ntdll
        version

        Detours
        WIL::WIL
  )
elseif(APPLE)
  find_library(CORE_FOUNDATION CoreFoundation)
  find_library(CORE_GRAPHICS CoreGraphics)

  target_link_libraries(
    RED4ext.Dll
      PUBLIC
        fishhook
        ${CORE_FOUNDATION}
        ${CORE_GRAPHICS}
        objc
  )
endif()

target_link_libraries(
  RED4ext.Dll
    PUBLIC
      fmt
      RED4ext::SDK
      redscript
      simdjson::simdjson
      spdlog
      toml11
      tsl::ordered_map
)
