# This helper registers unit tests next to their source modules.

include_guard(GLOBAL)

function(sharedstuff_add_unit_test module)
  set(target "sharedstuff_unit_${module}")
  add_executable(${target} "src/test_${module}.c")
  target_link_libraries(
    ${target} PRIVATE sharedstuff_build_tests sharedstuff_vendor_acutest SharedStuff::SharedStuff
  )

  add_test(NAME "sharedstuff.${module}" COMMAND ${target} WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")
  set_tests_properties(
    "sharedstuff.${module}"
    PROPERTIES LABELS "sharedstuff;unit"
               ENVIRONMENT_MODIFICATION "UBSAN_OPTIONS=string_prepend:print_stacktrace=1:"
  )

  if(PROJECT_IS_TOP_LEVEL)
    sharedstuff_enable_test_analysis(${target})
  endif()
endfunction()
