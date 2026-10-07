add_subdirectory(deps/fmt)
set_target_properties(fmt PROPERTIES FOLDER "Dependencies")
# fmt 10.1 trips Apple clang 21 consteval checks in its own sources; disable compile-time format checks.
target_compile_definitions(fmt PUBLIC FMT_CONSTEVAL=)

mark_as_advanced(
  FMT_CUDA_TEST
  FMT_DEBUG_POSTFIX
  FMT_DOC
  FMT_FUZZ
  FMT_INC_DIR
  FMT_INSTALL
  FMT_MODULE
  FMT_OS
  FMT_PEDANTIC
  FMT_SYSTEM_HEADERS
  FMT_TEST
  FMT_WERROR
)
