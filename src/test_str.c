#include "test_oom.h"

#include <acutest.h>
#include <stddef.h>
#include <string.h>

#include "shared/arena.h"
#include "shared/str.h"

/** Bytes of backing memory each test's arena covers. */
enum { MEMORY_LEN = 4096 };

/** Backing memory for the arena under test. */
static _Alignas(max_align_t) char memory[MEMORY_LEN];

/**
 * @brief Clones a string longer than a 64-byte arena holds. Run through `test_is_oom`.
 *
 * @param arg Unused.
 */
static void clone_past_arena(void* arg) {
  (void)arg;
  struct Arena arena = {.beg = memory, .end = memory + 64};
  (void)str_clone((struct Str){memory + 64, 64}, &arena);
}

/** Static table built with `STR_INIT`, the constant form of `S`. */
static const struct Str literal_table[] = {STR_INIT("a"), STR_INIT("bcd")};

// `S` and `STR_INIT` measure a literal without its terminator, and `str_from_cstr` measures up to
// the terminator.
static void test_literal_and_cstr_lengths(void) {
  TEST_CHECK(literal_table[0].len == 1);
  TEST_CHECK(str_eq(literal_table[1], S("bcd")));
  TEST_CHECK(S("abc").len == 3);
  TEST_CHECK(S("").len == 0);
  TEST_CHECK(str_from_cstr("abc").len == 3);
  TEST_CHECK(str_eq(str_from_cstr("abc"), S("abc")));
}

// Equality compares lengths and bytes, so a prefix is not equal and an embedded null byte counts.
static void test_eq_compares_length_and_bytes(void) {
  TEST_CHECK(str_eq(S("abc"), S("abc")));
  TEST_CHECK(!str_eq(S("abc"), S("ab")));
  TEST_CHECK(!str_eq(S("abc"), S("abd")));
  TEST_CHECK(str_eq(S("a\0b"), S("a\0b")));
  TEST_CHECK(!str_eq(S("a\0b"), S("a\0c")));
  TEST_CHECK(str_eq((struct Str){NULL, 0}, S("")));
}

// Comparison orders unsigned bytes, a prefix before its extensions, and equal strings as `0`.
static void test_compare_orders_bytes_then_length(void) {
  TEST_CHECK(str_compare(S("abc"), S("abd")) < 0);
  TEST_CHECK(str_compare(S("abd"), S("abc")) > 0);
  TEST_CHECK(str_compare(S("ab"), S("abc")) < 0);
  TEST_CHECK(str_compare(S("abc"), S("ab")) > 0);
  TEST_CHECK(str_compare(S("abc"), S("abc")) == 0);
  TEST_CHECK(str_compare(S(""), (struct Str){NULL, 0}) == 0);
  TEST_CHECK(str_compare(S("a"), S("\x80")) < 0);
}

// Prefix and suffix tests accept the empty affix and the whole string, and reject a longer one.
static void test_startswith_and_endswith(void) {
  TEST_CHECK(str_startswith(S("index.html"), S("index")));
  TEST_CHECK(str_startswith(S("index.html"), S("")));
  TEST_CHECK(str_startswith(S("index"), S("index")));
  TEST_CHECK(!str_startswith(S("index"), S("index.html")));
  TEST_CHECK(!str_startswith(S("index.html"), S("html")));
  TEST_CHECK(str_endswith(S("index.html"), S(".html")));
  TEST_CHECK(str_endswith(S("index.html"), S("")));
  TEST_CHECK(!str_endswith(S("html"), S(".html")));
  TEST_CHECK(!str_endswith(S("index.html"), S("index")));
}

// `str_head` and `str_tail` split one string at the same offset into two views that cover it.
static void test_head_and_tail_split_views(void) {
  const struct Str s = S("abcdef");
  TEST_CHECK(str_eq(str_head(s, 2), S("ab")));
  TEST_CHECK(str_eq(str_tail(s, 2), S("cdef")));
  TEST_CHECK(str_eq(str_head(s, 0), S("")));
  TEST_CHECK(str_eq(str_tail(s, 6), S("")));
  TEST_CHECK(str_tail(s, 2).data == s.data + 2);
  TEST_CHECK(str_tail((struct Str){NULL, 0}, 0).data == NULL);
}

// Cutting splits at the first separator only, and walking `tail` visits every field, including an
// empty one between two adjacent separators.
static void test_cut_walks_fields(void) {
  struct StrCut cut = str_cut(S("a/bc//d"), '/');
  TEST_CHECK(cut.is_found);
  TEST_CHECK(str_eq(cut.head, S("a")));
  TEST_CHECK(str_eq(cut.tail, S("bc//d")));
  cut = str_cut(cut.tail, '/');
  TEST_CHECK(str_eq(cut.head, S("bc")));
  cut = str_cut(cut.tail, '/');
  TEST_CHECK(cut.is_found);
  TEST_CHECK(str_eq(cut.head, S("")));
  cut = str_cut(cut.tail, '/');
  TEST_CHECK(!cut.is_found);
  TEST_CHECK(str_eq(cut.head, S("d")));
  TEST_CHECK(str_eq(cut.tail, S("")));
}

// A trailing separator yields a found cut with an empty tail, which tells it apart from no
// separator at all.
static void test_cut_trailing_separator_is_found(void) {
  const struct StrCut cut = str_cut(S("a/"), '/');
  TEST_CHECK(cut.is_found);
  TEST_CHECK(str_eq(cut.head, S("a")));
  TEST_CHECK(str_eq(cut.tail, S("")));
  TEST_CHECK(!str_cut(S(""), '/').is_found);
}

// A clone copies the bytes into the arena, so it compares equal but no longer aliases the source.
static void test_clone_copies_into_arena(void) {
  struct Arena arena = {.beg = memory, .end = memory + MEMORY_LEN};
  char source[] = "hello";
  const struct Str clone = str_clone((struct Str){source, 5}, &arena);
  source[0] = 'j';
  TEST_CHECK(str_eq(clone, S("hello")));
  TEST_CHECK(clone.data >= memory && clone.data < memory + MEMORY_LEN);
  TEST_CHECK(str_clone((struct Str){NULL, 0}, &arena).len == 0);
}

// Concatenating onto the arena's last allocation extends it in place, so a run of concatenations
// copies the head once.
static void test_concat_extends_last_allocation(void) {
  struct Arena arena = {.beg = memory, .end = memory + MEMORY_LEN};
  struct Str joined = str_concat(S("ab"), S("cd"), &arena);
  const char* const data = joined.data;
  joined = str_concat(joined, S("ef"), &arena);
  TEST_CHECK(joined.data == data);
  TEST_CHECK(str_eq(joined, S("abcdef")));
  TEST_CHECK(arena.beg == data + 6);
}

// A head that is not the last allocation is copied before the tail is appended, and the old bytes
// stay as they were.
static void test_concat_copies_older_head(void) {
  struct Arena arena = {.beg = memory, .end = memory + MEMORY_LEN};
  const struct Str head = str_clone(S("ab"), &arena);
  (void)arena_new(&arena, 1, char);
  const struct Str joined = str_concat(head, S("cd"), &arena);
  TEST_CHECK(joined.data != head.data);
  TEST_CHECK(str_eq(joined, S("abcd")));
  TEST_CHECK(str_eq(head, S("ab")));
}

// `str_cstr` appends a terminator after the copied bytes, and an empty string yields `""`.
static void test_cstr_terminates_copy(void) {
  struct Arena arena = {.beg = memory, .end = memory + MEMORY_LEN};
  TEST_CHECK(strcmp(str_cstr(str_head(S("abcdef"), 3), &arena), "abc") == 0);
  TEST_CHECK(strcmp(str_cstr((struct Str){NULL, 0}, &arena), "") == 0);
}

// `STR_FMT` prints exactly the string's bytes, without reading past them, and an empty string with
// no bytes prints nothing.
static void test_fmt_prints_exact_bytes(void) {
  char out[16];
  const struct Str head = str_head(S("abcdef"), 3);
  const struct Str empty = {NULL, 0};
  (void)snprintf(out, sizeof(out), "[%.*s][%.*s]", STR_FMT(head), STR_FMT(empty));
  TEST_CHECK(strcmp(out, "[abc][]") == 0);
}

// A clone that does not fit ends in `os_oom` rather than returning.
static void test_clone_past_arena_is_oom(void) {
  TEST_CHECK(test_is_oom(clone_past_arena, NULL));
}

TEST_LIST = {
    {"literal and cstr lengths", test_literal_and_cstr_lengths},
    {"eq compares length and bytes", test_eq_compares_length_and_bytes},
    {"compare orders bytes then length", test_compare_orders_bytes_then_length},
    {"startswith and endswith", test_startswith_and_endswith},
    {"head and tail split views", test_head_and_tail_split_views},
    {"cut walks fields", test_cut_walks_fields},
    {"cut trailing separator is found", test_cut_trailing_separator_is_found},
    {"clone copies into arena", test_clone_copies_into_arena},
    {"concat extends last allocation", test_concat_extends_last_allocation},
    {"concat copies older head", test_concat_copies_older_head},
    {"cstr terminates copy", test_cstr_terminates_copy},
    {"fmt prints exact bytes", test_fmt_prints_exact_bytes},
    {"clone past arena is oom", test_clone_past_arena_is_oom},
    {NULL, NULL},
};
