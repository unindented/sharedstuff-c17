# These targets provide shared compile and link requirements.

include_guard(GLOBAL)

include(CheckCCompilerFlag)

add_library(sharedstuff_build_project INTERFACE)
add_library(sharedstuff_build_tests INTERFACE)

foreach(role IN ITEMS sharedstuff_build_project sharedstuff_build_tests)
  # Require C17 or later. A parent project cannot reduce this requirement.
  target_compile_features(${role} INTERFACE c_std_17)

  target_compile_options(
    ${role}
    INTERFACE
      $<$<AND:$<C_COMPILER_ID:GNU,Clang,AppleClang>,$<CONFIG:Debug>>:-Og>
      "$<$<AND:$<C_COMPILER_ID:GNU,Clang,AppleClang>,$<CONFIG:Release,RelWithDebInfo,MinSizeRel>>:SHELL:-U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=3>"
      $<$<C_COMPILER_ID:GNU,Clang,AppleClang>:-Wall;-Wextra;-Wpedantic;-Wformat=2;-Wvla>
      $<$<C_COMPILER_ID:GNU>:-Wlogical-op;-Wduplicated-cond;-Wshift-overflow=2>
      $<$<C_COMPILER_ID:Clang,AppleClang>:-Wconditional-uninitialized;-Wassign-enum;-Wcomma>
  )
endforeach()

# GCC and Clang support these warnings in different versions. Check each warning before use so all
# supported compilers can build the project.
foreach(flag IN ITEMS -Wformat-signedness -Wjump-misses-init)
  string(MAKE_C_IDENTIFIER "sharedstuff_have_warning${flag}" cache_var)
  check_c_compiler_flag("${flag}" "${cache_var}")
  if(${cache_var})
    target_compile_options(sharedstuff_build_project INTERFACE "${flag}")
    target_compile_options(sharedstuff_build_tests INTERFACE "${flag}")
  endif()
endforeach()

target_compile_options(
  sharedstuff_build_project
  INTERFACE
    $<$<C_COMPILER_ID:GNU,Clang,AppleClang>:-Wconversion;-Wsign-conversion;-Wstrict-prototypes;-Wmissing-prototypes;-Wshadow;-Wundef;-Wcast-qual;-Wwrite-strings>
)

set(SHAREDSTUFF_SANITIZER
    "none"
    CACHE STRING "Sanitizer to instrument the build with"
)
set_property(CACHE SHAREDSTUFF_SANITIZER PROPERTY STRINGS none address thread)

if(SHAREDSTUFF_SANITIZER STREQUAL "none")
  set(sharedstuff_sanitizer_flags "")
elseif(SHAREDSTUFF_SANITIZER STREQUAL "address")
  set(sharedstuff_sanitizer_flags -fsanitize=address,undefined -fno-sanitize-recover=all
                                  -fno-omit-frame-pointer
  )
elseif(SHAREDSTUFF_SANITIZER STREQUAL "thread")
  set(sharedstuff_sanitizer_flags -fsanitize=thread -fno-sanitize-recover=all -fno-omit-frame-pointer)
else()
  message(FATAL_ERROR "SHAREDSTUFF_SANITIZER must be one of none, address, thread; got '${SHAREDSTUFF_SANITIZER}'")
endif()

foreach(role IN ITEMS sharedstuff_build_project sharedstuff_build_tests)
  target_compile_options(${role} INTERFACE ${sharedstuff_sanitizer_flags})
  target_link_options(${role} INTERFACE ${sharedstuff_sanitizer_flags})
endforeach()
