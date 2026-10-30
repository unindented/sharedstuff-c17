#include "test_oom.h"

#include <acutest.h>
#include <stddef.h>
#include <string.h>

#include "shared/arena.h"
#include "shared/buf.h"
#include "shared/log.h"
#include "shared/str.h"

/** Bytes of backing memory each test's arena covers. */
enum { MEMORY_SIZE = 4096 };

/** Bytes of the arena an error does not fit. */
enum { ARENA_SMALL_LEN = 16 };

/** Bytes of the value that makes a message long. */
enum { VALUE_LONG_SIZE = 2000 };

/** Backing memory for the arenas under test. */
static _Alignas(max_align_t) char memory[MEMORY_SIZE];

/**
 * @brief Records an error longer than an `ARENA_SMALL_LEN`-byte arena holds. Run through
 *        `test_is_oom`.
 *
 * @param arg Unused.
 */
static void log_past_arena(void* arg) {
  (void)arg;
  struct Arena arena = {.beg = memory, .end = memory + ARENA_SMALL_LEN};
  struct Log log = {.buf = {.arena = &arena}};
  log_error(&log, "an error longer than the arena");
}

// Each error is one line in the order it was recorded, and each one is counted.
static void test_errors_append_lines_and_count(void) {
  struct Arena arena = {.beg = memory, .end = memory + MEMORY_SIZE};
  struct Log log = {.buf = {.arena = &arena}};
  TEST_CHECK(log_error(&log, "first") == -1);
  const struct Str value = S("x");
  TEST_CHECK(log_error(&log, "second '%.*s' at %d", STR_FMT(value), 2) == -1);
  TEST_CHECK(log.errors == 2);
  TEST_CHECK(str_eq(buf_str(&log.buf), S("first\nsecond 'x' at 2\n")));
}

// A message is never truncated, however long its values are.
static void test_long_message_is_kept_whole(void) {
  struct Arena arena = {.beg = memory, .end = memory + MEMORY_SIZE};
  struct Log log = {.buf = {.arena = &arena}};
  char value[VALUE_LONG_SIZE];
  memset(value, 'v', sizeof(value));
  const struct Str long_value = {value, VALUE_LONG_SIZE};
  log_error(&log, "bad value: '%.*s'", STR_FMT(long_value));
  const struct Str text = buf_str(&log.buf);
  const struct Str prefix = S("bad value: '");
  TEST_ASSERT(text.len == prefix.len + VALUE_LONG_SIZE + 2);
  TEST_CHECK(str_startswith(text, prefix) && str_endswith(text, S("'\n")));
  TEST_CHECK(str_eq(str_head(str_tail(text, prefix.len), VALUE_LONG_SIZE), long_value));
}

// An error found inside a file names it after the message, and an empty file names nothing.
static void test_error_in_names_file(void) {
  struct Arena arena = {.beg = memory, .end = memory + MEMORY_SIZE};
  struct Log log = {.buf = {.arena = &arena}};
  const struct Str key = S("title");
  TEST_CHECK(log_error_in(&log, S("a.md"), "key '%.*s' must be a string", STR_FMT(key)) == -1);
  TEST_CHECK(log_error_in(&log, (struct Str){0}, "no file") == -1);
  TEST_CHECK(log.errors == 2);
  TEST_CHECK(str_eq(buf_str(&log.buf), S("key 'title' must be a string (in 'a.md')\nno file\n")));
}

// Appending one log to another keeps both sets of messages in order and adds the counts, and leaves
// the source as it was.
static void test_append_concatenates_logs(void) {
  struct Arena first_arena = {.beg = memory, .end = memory + MEMORY_SIZE / 2};
  struct Arena second_arena = {.beg = memory + MEMORY_SIZE / 2, .end = memory + MEMORY_SIZE};
  struct Log log = {.buf = {.arena = &first_arena}};
  struct Log source = {.buf = {.arena = &second_arena}};
  log_error(&log, "a");
  log_error(&source, "b");
  log_error(&source, "c");
  log_append(&log, &source);
  TEST_CHECK(log.errors == 3);
  TEST_CHECK(str_eq(buf_str(&log.buf), S("a\nb\nc\n")));
  TEST_CHECK(source.errors == 2);
  TEST_CHECK(str_eq(buf_str(&source.buf), S("b\nc\n")));
}

// An error that does not fit the log's arena ends in `os_oom` rather than being dropped or cut.
static void test_log_past_arena_is_oom(void) {
  TEST_CHECK(test_is_oom(log_past_arena, NULL));
}

TEST_LIST = {
    {"errors append lines and count", test_errors_append_lines_and_count},
    {"long message is kept whole", test_long_message_is_kept_whole},
    {"error in names file", test_error_in_names_file},
    {"append concatenates logs", test_append_concatenates_logs},
    {"log past arena is oom", test_log_past_arena_is_oom},
    {NULL, NULL},
};
