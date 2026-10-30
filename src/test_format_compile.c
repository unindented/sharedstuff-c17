// This file is compiled, never linked or run, by the `sharedstuff.format_compile.*` tests. Each
// test defines one `TEST_FORMAT_` macro and expects the compile to fail with the error that names
// the problem. With no macro defined, the file compiles, which shows that a failure comes from the
// rejected call and not from the setup.

#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>

#include "shared/buf.h"
#include "shared/log.h"
#include "shared/str.h"

void test_format_compile(struct Buf* buffer, struct Log* log, struct Str path, ptrdiff_t count);

/**
 * @brief Appends a message to `buffer` in two passes, as a program's own variadic wrapper would.
 *
 * @param buffer Buffer to append to. Must not be `NULL`.
 * @param format `printf` format. Must not be `NULL`.
 * @param ...    Arguments that the format converts.
 */
static void append_message(struct Buf* buffer, const char* format, ...)
    __attribute__((format(printf, 2, 3), nonnull(1, 2)));

static void append_message(struct Buf* buffer, const char* format, ...) {
  va_list args;
  va_start(args, format);
  const ptrdiff_t text_len = vsnprintf(NULL, 0, format, args);
  va_end(args);
  va_start(args, format);
  buf_vprintf_measured(buffer, text_len, format, args);
  va_end(args);
}

void test_format_compile(struct Buf* buffer, struct Log* log, struct Str path, ptrdiff_t count) {
  buf_printf(buffer, "read '%.*s' at %td, %zu bytes, %d%%", STR_FMT(path), count, sizeof(path), 1);
  (void)log_error(log, "failed to read '%.*s'", STR_FMT(path));
  (void)log_error_in(log, path, "no arguments");
  append_message(buffer, "count %td", count);
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
#elif defined(TEST_FORMAT_WRAPPER)
  append_message(buffer, "failed to read '%.*s'", path);
#endif
}
