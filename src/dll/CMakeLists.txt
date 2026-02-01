add_library(RED4ext.Dll SHARED)

set_target_properties(RED4ext.Dll PROPERTIES OUTPUT_NAME RED4ext)

if(APPLE)
  set_target_properties(RED4ext.Dll PROPERTIES 
    PREFIX ""
    SUFFIX ".dylib"
  )
endif()

configure_file(Version.hpp.in "${CMAKE_CURRENT_SOURCE_DIR}/Version.hpp" @ONLY)

file(GLOB_RECURSE HEADER_FILES *.hpp)
file(GLOB_RECURSE SOURCE_FILES *.cpp)

if(WIN32)
  file(GLOB_RECURSE RC_FILES *.rc)
endif()

source_group(_CMake REGULAR_EXPRESSION cmake_pch.*)
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${HEADER_FILES} ${SOURCE_FILES} ${RC_FILES})

target_include_directories(RED4ext.Dll PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_sources(RED4ext.Dll PRIVATE ${HEADER_FILES} ${SOURCE_FILES} ${RC_FILES})

target_precompile_headers(RED4ext.Dll PUBLIC stdafx.hpp)

if(WIN32)
  target_link_libraries(RED4ext.Dll
    PUBLIC
      version
      Detours
      WIL
  )
elseif(APPLE)
  target_link_libraries(RED4ext.Dll
    PUBLIC
      fishhook
  )

  if(RED4EXT_USE_FRIDA_GUM)
    target_link_libraries(RED4ext.Dll PUBLIC frida-gum)
  endif()
endif()

target_link_libraries(RED4ext.Dll
  PUBLIC
    fmt
    RED4ext::SDK
    simdjson::simdjson
    spdlog
    toml11
    tsl::ordered_map
    redscript
)

if(APPLE AND RED4EXT_USE_FRIDA_GADGET)
  target_compile_definitions(RED4ext.Dll PRIVATE RED4EXT_USE_FRIDA_GADGET)
endif()

if(APPLE)
  find_library(CORE_FOUNDATION CoreFoundation)
  target_link_libraries(RED4ext.Dll PUBLIC ${CORE_FOUNDATION})
endif()
