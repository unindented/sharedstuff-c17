<div align="center">
  <img src="media/logo.webp" height="300" alt="">
</div>

<h1 align="center"><code>sharedstuff</code></h1>

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

The project provides the static library target `SharedStuff::SharedStuff`. Consumers can use the source tree directly:

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

The library has four modules. Each one builds on the ones before it, so they are described in that order: the arena allocates memory, a string is a view of that memory, a buffer grows a string in an arena, and a log is a buffer of error messages.

### `<shared/arena.h>`: memory

**Why it exists.** A program that frees each allocation must say, for every pointer, who owns it and when it is freed, and every allocation can fail. The arena removes both problems. Memory comes from one region in order, nothing is freed piecemeal, and an allocation never returns `NULL`. Data lives as long as the region it was allocated in, unless it was allocated through a copy of the arena that is then dropped. `arena_discard` gives the pages behind a large buffer back early, which loses its bytes but leaves the buffer usable.

**Define `os_oom` first.** When an allocation does not fit, the arena calls `os_oom`, which must not return. The library does not define it, because only the application knows what running out of memory should print and which status it should exit with. Every program that links the library defines it once:

```c
_Noreturn void os_oom(void) {
  fputs("myprogram: out of memory\n", stderr);
  _Exit(EXIT_FAILURE);
}
```

**Get a region.** `arena_reserve` reserves address space only. The arena makes pages usable as allocations reach them, so a large reservation costs memory only for what is used, even under strict overcommit. `arena_split` takes part of a region as a region of its own, such as one for each thread, and `arena_release` returns the whole reservation to the system. When the system refuses the full size, as under `ulimit -v`, `arena_reserve_halving` reserves a power-of-two fraction of it instead. `arena_discard` gives the pages behind a large buffer back to the system while its arena lives on. A test lowers `arena_reserve_len_max` to stand in for an address-space limit. A static array works too, with `limit` left `NULL`:

```c
struct Arena arena;
if (arena_reserve(1 << 30, &arena) != 0) {
  // report the error and stop; nothing has been reserved
}

static char storage[1 << 16];
struct Arena fixed = {.beg = storage, .end = storage + sizeof storage};
```

**Allocate.** `arena_new(&arena, count, Type)` returns zeroed, aligned storage for `count` objects of `Type`. `arena_grow` doubles a growable array, in place when it is the last allocation, and `arena_extend` appends raw bytes right after the last allocation.

**Pass an arena by pointer to keep data, and by value for temporaries.** A function that returns data takes `struct Arena* perm` and allocates its result there. A function that only needs scratch space takes `struct Arena scratch` by value: it allocates through its own copy of the bounds, so everything it allocated is free again when it returns.

```c
struct Str render_title(struct Str name, struct Arena* perm, struct Arena scratch);
```

**Rules for correct use:**

- Never assume two allocations are next to each other. The arena puts a gap between them in sanitizer builds. Use `arena_grow` or `arena_extend` to grow something in place.
- Never let two threads allocate from the same arena. Give each thread its own region with `arena_split`.
- Never grow a buffer or log in an arena that a callee also gets by value. The callee allocates the same bytes that the buffer grows into, and the two overwrite each other without a crash.
- With the address sanitizer, an arena over a reserved region poisons the bytes it has not handed out, so an overflow past one allocation is reported. It cannot poison scratch memory again when a function returns, so a use of scratch memory after its function returns is not reported.

### `<shared/str.h>`: strings

**Why it exists.** A C string finds its length by searching for a `NUL`, so a substring needs a copy, and lengths are unsigned, so `a - b` can wrap to a huge value. A `struct Str` is a pointer and a signed `ptrdiff_t` length. A substring is a view of the original bytes, a `NUL` is an ordinary byte, and length arithmetic cannot wrap.

**Make one.** `S("literal")` makes a string from a literal, `STR_INIT("literal")` initializes one in a static table, and `str_from_cstr` wraps a C string from outside the program. The zero value, `(struct Str){0}`, is the empty string.

**Work with views.** `str_head`, `str_tail` and `str_cut` return views and copy nothing. `str_cut` splits at the first occurrence of a byte, and calling it again on the tail walks a delimited list:

```c
struct StrCut cut = {.tail = S("a,b,c")};
while (cut.tail.len > 0) {
  cut = str_cut(cut.tail, ',');
  // cut.head is "a", then "b", then "c"
}
```

`str_eq`, `str_compare`, `str_startswith` and `str_endswith` compare. `str_clone` and `str_concat` copy into an arena.

**Rules for correct use:**

- Pass a `struct Str` by value. It is two words.
- A view lives only as long as the bytes it points at. Copy it with `str_clone` before those bytes go away.
- Convert a length to `size_t` only at a libc, POSIX or vendored call, and check that it is not negative there.
- Make a C string with `str_cstr` only where an interface needs one, such as a path for a system call. A `NUL` inside the string ends the C string early, so check for one first if the input can contain it.
- Print a string with `%.*s` and `STR_FMT(s)`, which expands to its `int` length and its data. `STR_FMT` evaluates its argument twice, so pass a variable, not an expression with side effects. `%.*s` stops at a `NUL`.

### `<shared/buf.h>`: growable buffers

**Why it exists.** Output such as a rendered page or a message is built from many small appends of unknown total size. A `struct Buf` grows in an arena, in place while it is the arena's last allocation, so building a large output usually copies nothing.

**Use it.** Start a buffer with the arena it grows in, append to it, then read it back as a string:

```c
struct Buf out = {.arena = perm};
buf_append(&out, S("<h1>"));
buf_printf(&out, "%.*s", STR_FMT(title));
buf_append_char(&out, '\n');
struct Str html = buf_str(&out);
```

`buf_cstr` adds a terminator that `len` does not count, for an interface that wants a C string. `buf_vprintf` takes a `va_list`, for a function that forwards its own format arguments.

**Rules for correct use:**

- The buffer's arena must outlive the buffer, and the arena must be passed by pointer, never by value, while the buffer still grows.
- A pointer into the buffer taken before a growth still reads the old bytes, but later appends do not reach it. Take `buf_str` after the last append.
- `buf_printf` carries the `format` attribute. Compile with `-Wformat` so the compiler checks each format against its arguments.

### `<shared/log.h>`: error logs

**Why it exists.** A fixed error buffer truncates long messages and holds only the first error. A `struct Log` is a buffer of error lines with a count. A program can keep going after an error, report every error at once, and check `log.errors` before it does anything it cannot undo.

**Use it.** Give the log its own arena, record errors with `printf` formats, and check the count before the first irreversible step:

```c
struct Log log = {.buf = {.arena = &log_arena}};

if (load_config(path, &perm, scratch, &log) != 0) {
  // load_config called log_error and returned its -1
}
if (log.errors > 0) {
  fputs(buf_cstr(&log.buf), stderr);
  return EXIT_FAILURE;
}
```

Inside a fallible function, `return log_error(log, "failed to read '%.*s': %s", STR_FMT(path), cause);` records one line and returns `-1`. `log_error_in(log, file, ...)` appends ` (in '<file>')` to a message about an error inside a file. `log_error_v` takes a `va_list`, for a wrapper that adds context. `log_append` copies one log into another, such as a worker's log into the main log after the workers finish.

**Rules for correct use:**

- Take the log as the last parameter of a fallible function, and return `-1` from the function when it records an error.
- Put the log in an arena that nothing else allocates from by value. A region of its own is the simplest choice.
- Give each thread its own log, and merge the logs with `log_append` in a fixed order after the threads finish, so the output does not depend on scheduling.
- `log_error`, `log_error_v` and `log_error_in` carry the `format` attribute, and so must any wrapper that forwards a format to them.

## Contributing

### Prerequisites

- POSIX-like system with CMake 3.25 or later and a C17 compiler such as Clang or GCC.
- Optional build tools: `ninja` for the multi-config portability check.
- Optional quality tools: `clang-format`, `clang-tidy`, `cppcheck`.

Vendored dependencies are included under `vendor/`:

- [`acutest`](https://github.com/mity/acutest): Tests.

### Building

Presets keep every build out of the source tree, in `build/<preset>`. A build with `CC` set goes in `build/<preset>/<CC>` instead, such as `build/debug/gcc-14`, so each compiler keeps its own CMake cache: CMake reads `CC` only when it first configures a directory. clang-tidy runs only with Clang, because it rejects GCC's warning options. The commands below use eight parallel build jobs. Adjust that number for the machine.

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
ctest --preset debug -R buf -j 8
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

Workflow presets run the complete configure, build, and test sequences used by CI. CI runs `ci-multi` a second time with `CC=gcc-14` to check GCC:

- `cmake --workflow --preset ci-debug`: `Debug` build, linting, and all ASan/UBSan tests.
- `cmake --workflow --preset ci-tsan`: TSan build and all tests.
- `cmake --workflow --preset ci-release`: `Release` build.
- `cmake --workflow --preset ci-multi`: `Debug` and `RelWithDebInfo` builds and tests under the `Ninja Multi-Config` generator.

To run the same Linux workflows from a machine with Podman, build the pinned Ubuntu image:

```sh
podman build \
  --tag sharedstuff-linux-ci \
  --file Containerfile \
  .
```

Docker BuildKit accepts the same command with `docker` in place of `podman`. The image contains a snapshot of the source tree, so container builds do not mix Linux products with the host `build/` directory. Run each workflow preset in a fresh container:

```sh
podman run --rm sharedstuff-linux-ci ci-debug
podman run --rm sharedstuff-linux-ci ci-tsan
podman run --rm sharedstuff-linux-ci ci-release
podman run --rm sharedstuff-linux-ci ci-multi
podman run --rm -e CC=gcc-14 sharedstuff-linux-ci ci-multi
```

The image supports x86-64 and AArch64 hosts and pins LLVM 22 and GCC 14.
