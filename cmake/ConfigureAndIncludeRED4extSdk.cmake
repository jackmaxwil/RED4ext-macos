option(RED4EXT_USE_PCH "" ON)

if(APPLE)
  set(RED4EXT_USE_PCH OFF)
  set(RED4EXT_HEADER_ONLY ON CACHE BOOL "" FORCE)
  
  # RED4EXT_SDK_DIR is the SDK repository root; it defaults to the pinned deps/red4ext.sdk submodule
  # (jackmaxwil/RED4ext.SDK-macos). tools/cp-dev and scripts/create_release.sh pass the workspace checkout explicitly.
  set(RED4EXT_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/deps/red4ext.sdk" CACHE PATH "RED4ext.SDK repository root")
  set(RED4EXT_SDK_INCLUDE_DIR "${RED4EXT_SDK_DIR}/include")
  
  if(NOT EXISTS "${RED4EXT_SDK_INCLUDE_DIR}")
    message(FATAL_ERROR 
      "RED4ext.SDK not found at ${RED4EXT_SDK_DIR}\n"
      "Please run: git submodule update --init --recursive\n"
      "The SDK submodule should point to jackmaxwil/RED4ext.SDK-macos for macOS support.")
  endif()
  
  add_library(RED4ext.SDK INTERFACE)
  target_include_directories(RED4ext.SDK INTERFACE "${RED4EXT_SDK_INCLUDE_DIR}")
  add_library(RED4ext::SDK ALIAS RED4ext.SDK)
else()
  add_subdirectory(deps/red4ext.sdk)
endif()

set_target_properties(RED4ext.SDK PROPERTIES FOLDER "Dependencies")

mark_as_advanced(
  RED4EXT_BUILD_EXAMPLES
  RED4EXT_HEADER_ONLY
  RED4EXT_USE_PCH
  RED4EXT_INSTALL
)
