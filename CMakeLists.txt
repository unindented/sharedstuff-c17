cmake_minimum_required(VERSION 3.25...4.0)

# Set this target before `project()` because compiler checks use it. Builders can raise the value.
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

if(PROJECT_IS_TOP_LEVEL AND "${PROJECT_SOURCE_DIR}" STREQUAL "${PROJECT_BINARY_DIR}")
  message(FATAL_ERROR "in-source builds are unsupported; use a preset or '-B build/<name>'")
endif()

if(NOT DEFINED SHAREDSTUFF_BUILD_TESTING)
  set(SHAREDSTUFF_BUILD_TESTING "${PROJECT_IS_TOP_LEVEL}")
endif()
if(NOT DEFINED SHAREDSTUFF_INSTALL)
  set(SHAREDSTUFF_INSTALL "${PROJECT_IS_TOP_LEVEL}")
endif()

if(SHAREDSTUFF_BUILD_TESTING)
  if(PROJECT_IS_TOP_LEVEL)
    enable_testing()
  else()
    message(STATUS "SharedStuff tests are enabled as a child; the parent must call 'enable_testing()' at its top level")
  endif()
endif()

if(NOT DEFINED CMAKE_C_STANDARD)
  set(CMAKE_C_STANDARD 17)
endif()
if(NOT DEFINED CMAKE_C_STANDARD_REQUIRED)
  set(CMAKE_C_STANDARD_REQUIRED TRUE)
endif()
if(NOT DEFINED CMAKE_C_EXTENSIONS)
  set(CMAKE_C_EXTENSIONS FALSE)
endif()

set(CMAKE_LINK_LIBRARIES_ONLY_TARGETS TRUE)
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")

include(sharedstuff_build_profiles)

if(PROJECT_IS_TOP_LEVEL)
  include(sharedstuff_quality)
endif()
if(SHAREDSTUFF_BUILD_TESTING)
  include(sharedstuff_testing)
endif()

add_library(SharedStuff STATIC)
add_library(SharedStuff::SharedStuff ALIAS SharedStuff)

target_sources(
  SharedStuff
  PRIVATE src/arena.c src/buf.c src/log.c src/str.c
  PUBLIC FILE_SET HEADERS
         BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/include"
         FILES include/shared/arena.h include/shared/buf.h include/shared/log.h
               include/shared/str.h
)
target_compile_features(SharedStuff PUBLIC c_std_17)
target_include_directories(
  SharedStuff
  PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
         "$<INSTALL_INTERFACE:include>"
)
# The build profile is a build-tree detail, so the installed package does not name it.
target_link_libraries(SharedStuff PRIVATE "$<BUILD_INTERFACE:sharedstuff_build_project>")
# A static library instrumented with a sanitizer needs the sanitizer runtime in the final link, so
# whatever links the library gets the same link flags.
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

  foreach(module IN ITEMS arena buf log str)
    sharedstuff_add_unit_test(${module})
  endforeach()

  # The `format` attribute on `buf_printf` and `log_error` must make the compiler refuse a call
  # whose arguments disagree with its format. Each case compiles `src/test_format_compile.c` with
  # one macro defined and expects the compiler error in `expect`, in Clang's or GCC's wording. The
  # control case defines none and must compile. Fields are separated by `~`, because a regex holds
  # `|`.
  set(sharedstuff_format_compile_cases
      "control~"
      "raw_str~TEST_FORMAT_RAW_STR~type 'struct Str'"
      "missing_argument~TEST_FORMAT_MISSING_ARGUMENT~more '%' conversions than data arguments|expects a matching"
      "ptrdiff_as_int~TEST_FORMAT_PTRDIFF_AS_INT~type 'ptrdiff_t'"
      "nonliteral~TEST_FORMAT_NONLITERAL~format( string is)? not a string literal"
      "fmt_of_int~TEST_FORMAT_FMT_OF_INT~incompatible type"
      "forwarded~TEST_FORMAT_FORWARDED~type 'struct Str'"
  )
  foreach(compile_case IN LISTS sharedstuff_format_compile_cases)
    string(REPLACE "~" ";" compile_case "${compile_case}")
    list(GET compile_case 0 name)
    list(GET compile_case 1 define)
    set(expect "")
    if(define)
      list(GET compile_case 2 expect)
    endif()
    add_test(
      NAME "sharedstuff.format_compile.${name}"
      COMMAND
        "${CMAKE_COMMAND}" "-DCOMPILER=${CMAKE_C_COMPILER}"
        "-DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/src/test_format_compile.c"
        "-DINCLUDE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/include" "-DCASE=${define}" "-DEXPECT=${expect}"
        "-DSYSROOT=${CMAKE_OSX_SYSROOT}" -P
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/sharedstuff_compile_fail.cmake"
    )
    set_tests_properties("sharedstuff.format_compile.${name}" PROPERTIES LABELS "sharedstuff;unit")
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
