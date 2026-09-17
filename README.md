# `sharedstuff`

Some stuff I use in different C17 projects.

## LLM disclosure

> [!warning]
> I used LLMs extensively to create this project, mostly Claude Opus 4.8 and 5.

## Installation

To install the library, headers, and CMake package metadata:

```sh
cmake --preset release
cmake --build --preset release -j 8
cmake --install build/release --prefix /desired/prefix
```

## Usage

The project provides the static library target `SharedStuff::SharedStuff`. Its public headers are:

- `<shared/arena.h>`
- `<shared/string_buffer.h>`

Consumers can use the source tree directly:

```cmake
add_subdirectory(path/to/sharedstuff sharedstuff)
target_link_libraries(MyTarget PRIVATE SharedStuff::SharedStuff)
```

Or find an installed package:

```cmake
find_package(SharedStuff 0.1 CONFIG REQUIRED)
target_link_libraries(MyTarget PRIVATE SharedStuff::SharedStuff)
```

`SHAREDSTUFF_BUILD_TESTING` controls test creation and defaults to true only for a top-level build. `SHAREDSTUFF_INSTALL` controls install rules and has the same default. `SHAREDSTUFF_SANITIZER` accepts `none`, `address`, or `thread`.

## Contributing

### Prerequisites

- POSIX-like system with CMake 3.25 or later and a C17 compiler such as Clang or GCC.
- Optional build tools: `ninja` for the multi-config portability check.
- Optional quality tools: `clang-format`, `clang-tidy`, `cppcheck`.

Vendored dependencies are included under `vendor/`:

- [`acutest`](https://github.com/mity/acutest): Tests.

### Building

Presets keep every build out of the source tree. The commands below use eight parallel build jobs. Adjust that number for the machine.

#### Debug build

The debug build enables `AddressSanitizer` and `UndefinedBehaviorSanitizer`. It also runs `clang-tidy` and `cppcheck` during compilation when they are available.

```sh
cmake --preset debug
cmake --build --preset debug -j 8
```

The library is `build/debug/libSharedStuff.a`.

To run linting after configuring this preset:

```sh
cmake --build --preset lint
```

#### TSan build

The TSan build uses the `Debug` configuration and instruments the build for data races using `ThreadSanitizer`.

```sh
cmake --preset tsan
cmake --build --preset tsan -j 8
```

The library is `build/tsan/libSharedStuff.a`.

#### Release build

The release preset uses `RelWithDebInfo`, treats compiler warnings as errors, and does not build the tests.

```sh
cmake --preset release
cmake --build --preset release -j 8
```

The library is `build/release/libSharedStuff.a`.

#### Multi-config build

The multi-config preset uses the `Ninja Multi-Config` generator. One configured tree can build both configurations:

```sh
cmake --preset multi
cmake --build --preset multi-debug -j 8
cmake --build --preset multi-relwithdebinfo -j 8
```

The libraries are `build/multi/Debug/libSharedStuff.a` and `build/multi/RelWithDebInfo/libSharedStuff.a`.

### Testing

CTest registers the colocated unit test suite.

#### Debug tests

```sh
cmake --preset debug
cmake --build --preset debug -j 8
ctest --preset debug -j 8
```

To select part of the debug suite:

```sh
ctest --preset debug -L unit -j 8
ctest --preset debug -R arena -j 8
ctest --preset debug -R string_buffer -j 8
```

#### TSan tests

```sh
cmake --preset tsan
cmake --build --preset tsan -j 8
ctest --preset tsan -j 8
```

#### Multi-config tests

Each configuration must be named when building and testing:

```sh
cmake --preset multi

cmake --build --preset multi-debug -j 8
ctest --preset multi-debug -j 8

cmake --build --preset multi-relwithdebinfo -j 8
ctest --preset multi-relwithdebinfo -j 8
```

The `multi-relwithdebinfo` test preset exercises the same CMake configuration used by the release build. The release preset itself does not build tests.

#### CI workflows

Workflow presets run the complete configure, build, and test sequences used by CI:

- `cmake --workflow --preset ci-debug`: `Debug` build, linting, and all ASan/UBSan tests.
- `cmake --workflow --preset ci-tsan`: TSan build and all tests.
- `cmake --workflow --preset ci-release`: `Release` build.
- `cmake --workflow --preset ci-multi`: `Debug` and `RelWithDebInfo` builds and tests under the `Ninja Multi-Config` generator.
