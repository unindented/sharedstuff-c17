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
find_program(SHAREDSTUFF_CWRAP NAMES cwrap)
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

# The format and lint targets run clang-format and then cwrap, which refills comment prose to 100
# columns, the limit that clang-format only caps. A tool that is not installed is skipped.
if(SHAREDSTUFF_CLANG_FORMAT)
  set(sharedstuff_clang_format_fix
      COMMAND "${SHAREDSTUFF_CLANG_FORMAT}" -i ${sharedstuff_owned_sources}
  )
  set(sharedstuff_clang_format_check
      COMMAND "${SHAREDSTUFF_CLANG_FORMAT}" --dry-run --Werror ${sharedstuff_owned_sources}
  )
else()
  set(sharedstuff_clang_format_fix
      COMMAND "${CMAKE_COMMAND}" -E echo "clang-format not found, skipping"
  )
  set(sharedstuff_clang_format_check ${sharedstuff_clang_format_fix})
endif()

if(SHAREDSTUFF_CWRAP)
  set(sharedstuff_cwrap_fix
      COMMAND "${SHAREDSTUFF_CWRAP}" -i ${sharedstuff_owned_sources}
  )
  set(sharedstuff_cwrap_check
      COMMAND "${SHAREDSTUFF_CWRAP}" --check ${sharedstuff_owned_sources}
  )
else()
  set(sharedstuff_cwrap_fix COMMAND "${CMAKE_COMMAND}" -E echo "cwrap not found, skipping")
  set(sharedstuff_cwrap_check ${sharedstuff_cwrap_fix})
endif()

add_custom_target(
  sharedstuff_format ${sharedstuff_clang_format_fix} ${sharedstuff_cwrap_fix}
  COMMENT "Formatting first-party sources"
  COMMAND_EXPAND_LISTS VERBATIM
)
add_custom_target(
  sharedstuff_lint ${sharedstuff_clang_format_check} ${sharedstuff_cwrap_check}
  COMMENT "Checking first-party source formatting"
  COMMAND_EXPAND_LISTS VERBATIM
)
