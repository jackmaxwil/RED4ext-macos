set(FRIDA_GUM_DEVKIT_DIR "${PROJECT_SOURCE_DIR}/deps/frida-gum-devkit")

if(NOT EXISTS "${FRIDA_GUM_DEVKIT_DIR}/frida-gum.h")
  message(FATAL_ERROR "Frida Gum devkit not found at ${FRIDA_GUM_DEVKIT_DIR}. Expected frida-gum.h")
endif()

if(NOT EXISTS "${FRIDA_GUM_DEVKIT_DIR}/libfrida-gum.a")
  message(FATAL_ERROR "Frida Gum devkit not found at ${FRIDA_GUM_DEVKIT_DIR}. Expected libfrida-gum.a")
endif()

add_library(frida-gum STATIC IMPORTED GLOBAL)
set_target_properties(frida-gum PROPERTIES
  IMPORTED_LOCATION "${FRIDA_GUM_DEVKIT_DIR}/libfrida-gum.a"
)

target_include_directories(frida-gum INTERFACE "${FRIDA_GUM_DEVKIT_DIR}")
