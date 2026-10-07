add_subdirectory(dll)

if(WIN32)
  add_subdirectory(loader)
  add_subdirectory(playground)
endif()

if(APPLE)
  add_executable(red4ext_plugin_check plugin_check/main.cpp dll/Platform/PluginRequirements.cpp)
  target_include_directories(red4ext_plugin_check PRIVATE plugin_check)
  target_compile_definitions(red4ext_plugin_check PRIVATE RED4EXT_PLATFORM_MACOS)
  target_link_libraries(red4ext_plugin_check PRIVATE simdjson)
endif()
