cmake_minimum_required(VERSION 3.25...4.0)

# Match the consumers' default while allowing builders to raise it.
set(CMAKE_OSX_DEPLOYMENT_TARGET
    "13.0"
    CACHE STRING "Minimum macOS version the build targets"
)

project(
  SharedStuff
  VERSION 0.1.0
  DESCRIPTION "Some stuff I use in different C17 projects"
  HOMEPAGE_URL "https://github.com/unindented/sharedstuff-c17"
  LANGUAGES C
)

if(PROJECT_IS_TOP_LEVEL)
  list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")
  include(sharedstuff_quality)
endif()

if(PROJECT_IS_TOP_LEVEL AND "${PROJECT_SOURCE_DIR}" STREQUAL "${PROJECT_BINARY_DIR}")
  message(FATAL_ERROR "in-source builds are unsupported; use a preset or '-B build/<name>'")
endif()

option(SHAREDSTUFF_BUILD_TESTING "Build the SharedStuff tests" "${PROJECT_IS_TOP_LEVEL}")
option(SHAREDSTUFF_INSTALL "Generate install rules for SharedStuff" "${PROJECT_IS_TOP_LEVEL}")

if(SHAREDSTUFF_BUILD_TESTING)
  if(PROJECT_IS_TOP_LEVEL)
    enable_testing()
  else()
    message(
      STATUS "SharedStuff tests are enabled as a child; the parent must call 'enable_testing()'"
    )
  endif()
endif()

add_library(SharedStuff STATIC)
add_library(SharedStuff::SharedStuff ALIAS SharedStuff)

target_sources(
  SharedStuff
  PRIVATE src/arena.c src/string_buffer.c
  PUBLIC FILE_SET HEADERS
         BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/include"
         FILES include/shared/arena.h include/shared/string_buffer.h
)
target_compile_features(SharedStuff PUBLIC c_std_17)
target_include_directories(
  SharedStuff
  PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
         "$<INSTALL_INTERFACE:include>"
)

target_compile_options(
  SharedStuff
  PRIVATE
    $<$<AND:$<C_COMPILER_ID:GNU,Clang,AppleClang>,$<CONFIG:Debug>>:-Og>
    "$<$<AND:$<C_COMPILER_ID:GNU,Clang,AppleClang>,$<CONFIG:Release,RelWithDebInfo,MinSizeRel>>:SHELL:-U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=3>"
    $<$<C_COMPILER_ID:GNU,Clang,AppleClang>:-Wall;-Wextra;-Wpedantic;-Wformat=2;-Wvla>
    $<$<C_COMPILER_ID:GNU,Clang,AppleClang>:-Wconversion;-Wsign-conversion;-Wstrict-prototypes;-Wmissing-prototypes;-Wshadow;-Wundef;-Wcast-qual;-Wwrite-strings>
    $<$<C_COMPILER_ID:GNU>:-Wlogical-op;-Wduplicated-cond;-Wshift-overflow=2>
    $<$<C_COMPILER_ID:Clang,AppleClang>:-Wconditional-uninitialized;-Wassign-enum;-Wcomma>
)

set(SHAREDSTUFF_SANITIZER
    "none"
    CACHE STRING "Sanitizer to instrument the shared library with"
)
set_property(CACHE SHAREDSTUFF_SANITIZER PROPERTY STRINGS none address thread)

if(SHAREDSTUFF_SANITIZER STREQUAL "none")
  set(sharedstuff_sanitizer_flags "")
elseif(SHAREDSTUFF_SANITIZER STREQUAL "address")
  set(sharedstuff_sanitizer_flags -fsanitize=address,undefined -fno-sanitize-recover=all
                                  -fno-omit-frame-pointer
  )
elseif(SHAREDSTUFF_SANITIZER STREQUAL "thread")
  set(sharedstuff_sanitizer_flags -fsanitize=thread -fno-sanitize-recover=all
                                  -fno-omit-frame-pointer
  )
else()
  message(
    FATAL_ERROR
      "SHAREDSTUFF_SANITIZER must be one of none, address, thread; got '${SHAREDSTUFF_SANITIZER}'"
  )
endif()

target_compile_options(SharedStuff PRIVATE ${sharedstuff_sanitizer_flags})
target_link_options(SharedStuff PUBLIC ${sharedstuff_sanitizer_flags})

if(PROJECT_IS_TOP_LEVEL)
  sharedstuff_enable_project_analysis(SharedStuff)
endif()

if(SHAREDSTUFF_BUILD_TESTING)
  add_library(sharedstuff_vendor_acutest INTERFACE)
  target_include_directories(
    sharedstuff_vendor_acutest SYSTEM
    INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/vendor/acutest"
  )

  foreach(module IN ITEMS arena string_buffer)
    add_executable("sharedstuff_unit_${module}" "src/test_${module}.c")
    target_compile_features("sharedstuff_unit_${module}" PRIVATE c_std_17)
    target_compile_options(
      "sharedstuff_unit_${module}"
      PRIVATE $<$<C_COMPILER_ID:GNU,Clang,AppleClang>:-Wall;-Wextra;-Wpedantic;-Wformat=2;-Wvla>
    )
    target_link_libraries(
      "sharedstuff_unit_${module}"
      PRIVATE SharedStuff::SharedStuff sharedstuff_vendor_acutest
    )
    if(PROJECT_IS_TOP_LEVEL)
      sharedstuff_enable_test_analysis("sharedstuff_unit_${module}")
    endif()
    add_test(NAME "sharedstuff.${module}" COMMAND "sharedstuff_unit_${module}")
    set_tests_properties(
      "sharedstuff.${module}"
      PROPERTIES LABELS "sharedstuff;unit"
                 ENVIRONMENT_MODIFICATION
                 "ASAN_OPTIONS=string_prepend:allocator_may_return_null=1:;TSAN_OPTIONS=string_prepend:allocator_may_return_null=1:;UBSAN_OPTIONS=string_prepend:print_stacktrace=1:"
    )
  endforeach()
endif()

if(SHAREDSTUFF_INSTALL)
  include(CMakePackageConfigHelpers)
  include(GNUInstallDirs)

  install(
    TARGETS SharedStuff
    EXPORT SharedStuffTargets
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    FILE_SET HEADERS DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
  )
  install(
    EXPORT SharedStuffTargets
    FILE SharedStuffTargets.cmake
    NAMESPACE SharedStuff::
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/SharedStuff"
  )

  configure_package_config_file(
    cmake/SharedStuffConfig.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/SharedStuffConfig.cmake"
    INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/SharedStuff"
  )
  write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/SharedStuffConfigVersion.cmake"
    COMPATIBILITY SameMajorVersion
  )
  install(
    FILES "${CMAKE_CURRENT_BINARY_DIR}/SharedStuffConfig.cmake"
          "${CMAKE_CURRENT_BINARY_DIR}/SharedStuffConfigVersion.cmake"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/SharedStuff"
  )
endif()
