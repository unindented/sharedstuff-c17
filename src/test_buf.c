#include "test_oom.h"

#include <acutest.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "shared/arena.h"
#include "shared/buf.h"
#include "shared/str.h"

/** Bytes of backing memory each test's arena covers. */
enum { MEMORY_LEN = 4096 };

/** Backing memory for the arena under test. */
static _Alignas(max_align_t) char memory[MEMORY_LEN];

/**
 * @brief Appends a message in two passes through `buf_vprintf_measured`, as a program's own
 *        variadic wrapper does.
 *
 * @param buffer Buffer to append to. Must not be `NULL`.
 * @param format `printf` format. Must not be `NULL`.
 * @param ...    Arguments that the format converts.
 */
static void append_formatted(struct Buf* buffer, const char* format, ...)
    __attribute__((format(printf, 2, 3), nonnull(1, 2)));

static void append_formatted(struct Buf* buffer, const char* format, ...) {
  va_list args;
  va_start(args, format);
  const ptrdiff_t text_len = vsnprintf(NULL, 0, format, args);
  va_end(args);
  va_start(args, format);
  buf_vprintf_measured(buffer, text_len, format, args);
  va_end(args);
}

/**
 * @brief Appends one byte more than a 64-byte arena holds. Run through `test_is_oom`.
 *
 * @param arg Unused.
 */
static void append_past_arena(void* arg) {
  (void)arg;
  struct Arena arena = {.beg = memory, .end = memory + 64};
  struct Buf buffer = {.arena = &arena};
  for (int i = 0; i < 65; i++) {
    buf_append_char(&buffer, 'x');
  }
}

/**
 * @brief Appends exactly as many bytes as a 64-byte arena holds. Run through `test_is_oom`.
 *
 * @param arg Unused.
 */
static void append_to_fill_arena(void* arg) {
  (void)arg;
  struct Arena arena = {.beg = memory, .end = memory + 64};
  struct Buf buffer = {.arena = &arena};
  for (int i = 0; i < 64; i++) {
    buf_append_char(&buffer, 'x');
  }
}

/**
 * @brief Prints exactly as many bytes as a 64-byte arena holds. Run through `test_is_oom`.
 *
 * @param arg Unused.
 */
static void printf_to_fill_arena(void* arg) {
  (void)arg;
  struct Arena arena = {.beg = memory, .end = memory + 64};
  struct Buf buffer = {.arena = &arena};
  buf_printf(&buffer, "%64s", "x");
}

/**
 * @brief Prints one byte fewer than a 64-byte arena holds. Run through `test_is_oom`.
 *
 * @param arg Unused.
 */
static void printf_short_of_arena(void* arg) {
  (void)arg;
  struct Arena arena = {.beg = memory, .end = memory + 64};
  struct Buf buffer = {.arena = &arena};
  buf_printf(&buffer, "%63s", "x");
}

// Both append variants build one run of bytes, `buf_append` copies exactly the string's length,
// `buf_str` views what was appended, and `buf_cstr` terminates without counting the terminator.
static void test_append_builds_terminated_bytes(void) {
  struct Arena arena = {.beg = memory, .end = memory + MEMORY_LEN};
  struct Buf buffer = {.arena = &arena};
  buf_append(&buffer, S("foo"));
  buf_append(&buffer, (struct Str){"bar!!", 3});
  buf_append_char(&buffer, '!');
  TEST_CHECK(buffer.len == 7);
  TEST_CHECK(str_eq(buf_str(&buffer), S("foobar!")));
  TEST_CHECK(strcmp(buf_cstr(&buffer), "foobar!") == 0);
  TEST_CHECK(buffer.len == 7);

  buf_append_char(&buffer, '?');
  TEST_CHECK(strcmp(buf_cstr(&buffer), "foobar!?") == 0);
}

// A buffer nothing was appended to yields an empty string, and a zero-length append accepts a
// `NULL` source.
static void test_empty_buffer_yields_empty_string(void) {
  struct Arena arena = {.beg = memory, .end = memory + MEMORY_LEN};
  struct Buf buffer = {.arena = &arena};
  buf_append(&buffer, (struct Str){NULL, 0});
  TEST_CHECK(strcmp(buf_cstr(&buffer), "") == 0);
  TEST_CHECK(buffer.len == 0);
}

// While the buffer is the arena's last allocation it grows in place: `data` never moves, and the
// arena's free space starts right after the capacity.
static void test_last_allocation_grows_in_place(void) {
  struct Arena arena = {.beg = memory, .end = memory + MEMORY_LEN};
  struct Buf buffer = {.arena = &arena};
  buf_append_char(&buffer, 'a');
  char* const data = buffer.data;
  for (int i = 0; i < 1000; i++) {
    buf_append_char(&buffer, 'b');
  }
  TEST_CHECK(buffer.data == data);
  TEST_CHECK(buffer.len == 1001);
  TEST_CHECK(arena.beg == buffer.data + buffer.cap);
}

// A buffer that is no longer the last allocation moves when it grows and keeps its bytes.
static void test_interleaved_allocation_moves_buffer(void) {
  struct Arena arena = {.beg = memory, .end = memory + MEMORY_LEN};
  struct Buf buffer = {.arena = &arena};
  buf_append(&buffer, S("abc"));
  char* const data = buffer.data;
  (void)arena_new(&arena, 1, char);
  for (int i = 0; i < 10; i++) {
    buf_append_char(&buffer, 'd');
  }
  TEST_CHECK(buffer.data != data);
  TEST_CHECK(strcmp(buf_cstr(&buffer), "abcdddddddddd") == 0);
}

// Appending the buffer's own bytes is safe, because growth leaves the old bytes in place.
static void test_self_append_survives_growth(void) {
  struct Arena arena = {.beg = memory, .end = memory + MEMORY_LEN};
  struct Buf buffer = {.arena = &arena};
  buf_append(&buffer, S("ab"));
  (void)arena_new(&arena, 1, char);
  buf_append(&buffer, buf_str(&buffer));
  TEST_CHECK(strcmp(buf_cstr(&buffer), "abab") == 0);
}

// `buf_printf` appends what the format produces: a string through `STR_FMT`, each integer width
// with its own conversion, a C string, and `%%` as one `%`.
static void test_printf_appends_formatted_text(void) {
  struct Arena arena = {.beg = memory, .end = memory + MEMORY_LEN};
  struct Buf buffer = {.arena = &arena};
  const struct Str path = S("a.md");
  const ptrdiff_t offset = -3;
  const size_t size = 7;
  buf_append(&buffer, S("> "));
  buf_printf(&buffer, "read '%.*s' at %td, %zu bytes, %d%% %s", STR_FMT(path), offset, size, 50,
             "done");
  TEST_CHECK(str_eq(buf_str(&buffer), S("> read 'a.md' at -3, 7 bytes, 50% done")));
}

// A wrapper that measures its text and passes its list to `buf_vprintf_measured` appends what
// `buf_printf` does.
static void test_measured_printf_matches_printf(void) {
  struct Arena arena = {.beg = memory, .end = memory + MEMORY_LEN};
  struct Buf buffer = {.arena = &arena};
  const struct Str key = S("x");
  const ptrdiff_t offset = 3;
  append_formatted(&buffer, "bad key '%.*s' at %td;", STR_FMT(key), offset);
  buf_printf(&buffer, "bad key '%.*s' at %td;", STR_FMT(key), offset);
  TEST_CHECK(str_eq(buf_str(&buffer), S("bad key 'x' at 3;bad key 'x' at 3;")));
}

// While the buffer is the arena's last allocation, a message longer than its capacity grows it in
// place, and the whole message is kept.
static void test_printf_grows_in_place(void) {
  struct Arena arena = {.beg = memory, .end = memory + MEMORY_LEN};
  struct Buf buffer = {.arena = &arena};
  buf_append_char(&buffer, '[');
  char* const data = buffer.data;
  char value[2000];
  memset(value, 'v', sizeof(value));
  const struct Str long_value = {value, (ptrdiff_t)sizeof(value)};
  buf_printf(&buffer, "%.*s]", STR_FMT(long_value));
  TEST_CHECK(buffer.data == data);
  TEST_CHECK(buffer.len == (ptrdiff_t)sizeof(value) + 2);
  TEST_CHECK(arena.beg == buffer.data + buffer.cap);
  TEST_CHECK(str_startswith(buf_str(&buffer), S("[vvv")));
  TEST_CHECK(str_endswith(buf_str(&buffer), S("vvv]")));
}

// A message whose string points into the buffer itself survives the move that growth causes.
static void test_printf_self_reference_survives_growth(void) {
  struct Arena arena = {.beg = memory, .end = memory + MEMORY_LEN};
  struct Buf buffer = {.arena = &arena};
  buf_append(&buffer, S("ab"));
  (void)arena_new(&arena, 1, char);
  const struct Str own = buf_str(&buffer);
  buf_printf(&buffer, "-%.*s", STR_FMT(own));
  TEST_CHECK(strcmp(buf_cstr(&buffer), "ab-ab") == 0);
}

// `%.*s` stops at the first null byte, so a string that holds one prints only the bytes before it.
// The programs reject a null byte where text enters, so no message holds one.
static void test_printf_string_stops_at_nul(void) {
  struct Arena arena = {.beg = memory, .end = memory + MEMORY_LEN};
  struct Buf buffer = {.arena = &arena};
  const struct Str with_nul = {"a\0b", 3};
  buf_printf(&buffer, "'%.*s'", STR_FMT(with_nul));
  TEST_CHECK(str_eq(buf_str(&buffer), S("'a'")));
}

// The buffer can fill its arena to the last byte, because its storage skips the zeroing allocator
// and its exact-fit refusal. One byte more ends in `os_oom`.
static void test_growth_past_arena_is_oom(void) {
  TEST_CHECK(!test_is_oom(append_to_fill_arena, NULL));
  TEST_CHECK(test_is_oom(append_past_arena, NULL));
}

// `buf_printf` needs one byte past its text for the terminator that `vsnprintf` writes, so text one
// byte short of the arena fits and text that fills it ends in `os_oom`.
static void test_printf_past_arena_is_oom(void) {
  TEST_CHECK(!test_is_oom(printf_short_of_arena, NULL));
  TEST_CHECK(test_is_oom(printf_to_fill_arena, NULL));
}

TEST_LIST = {
    {"append builds terminated bytes", test_append_builds_terminated_bytes},
    {"empty buffer yields empty string", test_empty_buffer_yields_empty_string},
    {"last allocation grows in place", test_last_allocation_grows_in_place},
    {"interleaved allocation moves buffer", test_interleaved_allocation_moves_buffer},
    {"self append survives growth", test_self_append_survives_growth},
    {"printf appends formatted text", test_printf_appends_formatted_text},
    {"measured printf matches printf", test_measured_printf_matches_printf},
    {"printf grows in place", test_printf_grows_in_place},
    {"printf self reference survives growth", test_printf_self_reference_survives_growth},
    {"printf string stops at nul", test_printf_string_stops_at_nul},
    {"growth past arena is oom", test_growth_past_arena_is_oom},
    {"printf past arena is oom", test_printf_past_arena_is_oom},
    {NULL, NULL},
};
