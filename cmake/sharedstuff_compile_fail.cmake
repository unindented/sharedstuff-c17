# This script runs one compile-fail test. It compiles `SOURCE` with `-D${CASE}` and passes only
# when the compile fails and the compiler output matches `EXPECT`. When `CASE` is empty, it passes
# only when the compile succeeds. The caller passes `COMPILER`, `SOURCE`, `INCLUDE_DIR`, `CASE`,
# `EXPECT` and `SYSROOT` with `-D`.

# `-Wformat=2` is the format checking the projects build with: argument count and types,
# `-Wformat-nonliteral` and `-Wformat-security`.
set(command "${COMPILER}" -std=c17 -fsyntax-only -Wall -Wformat=2 -Werror -I "${INCLUDE_DIR}")
if(SYSROOT)
  list(APPEND command -isysroot "${SYSROOT}")
endif()
if(CASE)
  list(APPEND command "-D${CASE}")
endif()
list(APPEND command "${SOURCE}")

# The C locale keeps the compiler's diagnostics in English with ASCII quotes, which `EXPECT`
# matches. Under a UTF-8 locale GCC quotes with typographic marks instead.
execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env LC_ALL=C ${command}
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE output
)

if(NOT CASE)
  if(NOT result EQUAL 0)
    message(FATAL_ERROR "the control compile failed:\n${output}")
  endif()
  return()
endif()
if(result EQUAL 0)
  message(FATAL_ERROR "'${CASE}' compiled, but it must not")
endif()
if(NOT output MATCHES "${EXPECT}")
  message(FATAL_ERROR "'${CASE}' failed to compile without an error that matches '${EXPECT}':\n${output}")
endif()
