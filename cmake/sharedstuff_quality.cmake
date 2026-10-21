# This file configures formatting and static analysis for first-party sources.

include_guard(GLOBAL)

set(sharedstuff_owned_sources
    "${PROJECT_SOURCE_DIR}/include/shared/arena.h"
    "${PROJECT_SOURCE_DIR}/include/shared/buf.h"
    "${PROJECT_SOURCE_DIR}/include/shared/log.h"
    "${PROJECT_SOURCE_DIR}/include/shared/str.h"
    "${PROJECT_SOURCE_DIR}/src/arena.c"
    "${PROJECT_SOURCE_DIR}/src/asan.h"
    "${PROJECT_SOURCE_DIR}/src/buf.c"
    "${PROJECT_SOURCE_DIR}/src/log.c"
    "${PROJECT_SOURCE_DIR}/src/str.c"
    "${PROJECT_SOURCE_DIR}/src/test_arena.c"
    "${PROJECT_SOURCE_DIR}/src/test_buf.c"
    "${PROJECT_SOURCE_DIR}/src/test_log.c"
    "${PROJECT_SOURCE_DIR}/src/test_format_compile.c"
    "${PROJECT_SOURCE_DIR}/src/test_str.c"
    "${PROJECT_SOURCE_DIR}/src/test_oom.h"
)

find_program(SHAREDSTUFF_CLANG_FORMAT NAMES clang-format-22 clang-format)
find_program(SHAREDSTUFF_CLANG_TIDY NAMES clang-tidy-22 clang-tidy)
find_program(SHAREDSTUFF_CPPCHECK NAMES cppcheck)

set(sharedstuff_clang_tidy_command "${SHAREDSTUFF_CLANG_TIDY}")
set(sharedstuff_cppcheck_command "${SHAREDSTUFF_CPPCHECK}")
set(sharedstuff_cppcheck_standard "c17")
if(CMAKE_CROSSCOMPILING)
  message(VERBOSE "cross compiling; clang-tidy and cppcheck are disabled")
  set(sharedstuff_clang_tidy_command "")
  set(sharedstuff_cppcheck_command "")
endif()

# clang-tidy reads the compiler's command line, and it rejects GCC-only warning options such as
# `-Wlogical-op` as errors. cppcheck does not read the flags, so it runs with any compiler.
if(NOT CMAKE_C_COMPILER_ID MATCHES "Clang")
  message(VERBOSE "compiler is ${CMAKE_C_COMPILER_ID}, not Clang; clang-tidy is disabled")
  set(sharedstuff_clang_tidy_command "")
endif()

# Production and test targets run cppcheck with the same arguments.
set(sharedstuff_cppcheck_property
    "${sharedstuff_cppcheck_command};--enable=warning,performance,portability;--std=${sharedstuff_cppcheck_standard};--error-exitcode=1;--quiet"
)

function(sharedstuff_enable_project_analysis target)
  if(sharedstuff_clang_tidy_command)
    set_property(
      TARGET ${target}
      PROPERTY C_CLANG_TIDY
               "${sharedstuff_clang_tidy_command};--quiet;--config-file=${PROJECT_SOURCE_DIR}/.clang-tidy"
    )
  endif()
  if(sharedstuff_cppcheck_command)
    set_property(TARGET ${target} PROPERTY C_CPPCHECK "${sharedstuff_cppcheck_property}")
  endif()
endfunction()

function(sharedstuff_enable_test_analysis target)
  if(sharedstuff_cppcheck_command)
    set_property(TARGET ${target} PROPERTY C_CPPCHECK "${sharedstuff_cppcheck_property}")
  endif()
endfunction()

if(SHAREDSTUFF_CLANG_FORMAT)
  add_custom_target(
    sharedstuff_format
    COMMAND "${SHAREDSTUFF_CLANG_FORMAT}" -i ${sharedstuff_owned_sources}
    COMMENT "Formatting first-party sources"
    COMMAND_EXPAND_LISTS VERBATIM
  )
  add_custom_target(
    sharedstuff_lint
    COMMAND "${SHAREDSTUFF_CLANG_FORMAT}" --dry-run --Werror ${sharedstuff_owned_sources}
    COMMENT "Checking first-party source formatting"
    COMMAND_EXPAND_LISTS VERBATIM
  )
else()
  add_custom_target(sharedstuff_format COMMAND "${CMAKE_COMMAND}" -E echo "clang-format not found; skipping")
  add_custom_target(sharedstuff_lint COMMAND "${CMAKE_COMMAND}" -E echo "clang-format not found; skipping")
endif()
