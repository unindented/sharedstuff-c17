// This file is compiled, never linked or run, by the `sharedstuff.format_compile.*` tests. Each
// test defines one `TEST_FORMAT_` macro and expects the compile to fail with the error that names
// the problem. With no macro defined, the file compiles, which shows that a failure comes from the
// rejected call and not from the setup.

#include <stdarg.h>
#include <stddef.h>

#include "shared/buf.h"
#include "shared/log.h"
#include "shared/str.h"

void test_format_compile(struct Buf* buffer, struct Log* log, struct Str path, ptrdiff_t count);

/**
 * @brief Forwards a message to `log_error_v`, as a program's own wrapper would.
 *
 * @param log    Log to append to. Must not be `NULL`.
 * @param format `printf` format. Must not be `NULL`.
 * @param ...    Arguments that the format converts.
 */
static void forward_error(struct Log* log, const char* format, ...)
    __attribute__((format(printf, 2, 3), nonnull(1, 2)));

static void forward_error(struct Log* log, const char* format, ...) {
  va_list args;
  va_start(args, format);
  (void)log_error_v(log, format, args);
  va_end(args);
}

void test_format_compile(struct Buf* buffer, struct Log* log, struct Str path, ptrdiff_t count) {
  buf_printf(buffer, "read '%.*s' at %td, %zu bytes, %d%%", STR_FMT(path), count, sizeof(path), 1);
  (void)log_error(log, "failed to read '%.*s'", STR_FMT(path));
  (void)log_error_in(log, path, "no arguments");
  forward_error(log, "count %td", count);
#if defined(TEST_FORMAT_RAW_STR)
  (void)log_error(log, "failed to read '%.*s'", path);
#elif defined(TEST_FORMAT_MISSING_ARGUMENT)
  (void)log_error(log, "failed to read '%.*s': %s", STR_FMT(path));
#elif defined(TEST_FORMAT_PTRDIFF_AS_INT)
  buf_printf(buffer, "count %d", count);
#elif defined(TEST_FORMAT_NONLITERAL)
  const char* format = "count";
  (void)log_error(log, format);
#elif defined(TEST_FORMAT_FMT_OF_INT)
  (void)log_error(log, "count '%.*s'", STR_FMT(count));
#elif defined(TEST_FORMAT_FORWARDED)
  forward_error(log, "failed to read '%.*s'", path);
#endif
}
