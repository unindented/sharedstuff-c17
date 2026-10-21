#include "test_oom.h"

#include <acutest.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "asan.h"
#include "shared/arena.h"
#include "shared/buf.h"

/** Bytes of backing memory each test's arena covers. */
enum { MEMORY_LEN = 4096 };

/** Bytes of address space each test that reserves one asks for. */
enum { RESERVE_LEN = 8 * 1024 * 1024 };

/** Bytes of the buffer the discard tests give back, enough that its middle lies in a whole page. */
enum { DISCARD_LEN = 256 * 1024 };

/** Backing memory for the arena under test, aligned for any type. */
static _Alignas(max_align_t) char memory[MEMORY_LEN];

/**
 * @brief Returns an arena over the first `len` bytes of `memory`, filled with a non-zero pattern.
 *
 * The pattern lets a test tell zeroed storage apart from memory that merely started out zero.
 *
 * @param len Number of bytes the arena covers. At most `MEMORY_LEN`.
 * @return An arena whose free space is `memory[0, len)`.
 */
static struct Arena arena_over_memory(ptrdiff_t len) {
  memset(memory, 0xA5, sizeof(memory));
  return (struct Arena){.beg = memory, .end = memory + len};
}

/**
 * @brief Reports whether a pointer is a multiple of `align`.
 *
 * @param ptr   Pointer to inspect.
 * @param align Alignment in bytes. Must be a power of two.
 * @return `true` when `ptr` is aligned to `align`, or `false` otherwise.
 */
static bool is_aligned(const void* ptr, uintptr_t align) {
  return ((uintptr_t)ptr & (align - 1)) == 0;
}

/** Request one `test_is_oom` child makes: `count` objects of `size` bytes from `bytes` of space. */
struct AllocRequest {
  /** Bytes of free space the arena starts with. */
  ptrdiff_t bytes;

  /** Number of objects requested. */
  ptrdiff_t count;

  /** Size of one object in bytes. */
  ptrdiff_t size;
};

/**
 * @brief Makes the allocation an `AllocRequest` describes. Run through `test_is_oom`.
 *
 * @param arg `struct AllocRequest*` to make. Must not be `NULL`.
 */
static void alloc_request(void* arg) {
  const struct AllocRequest* request = arg;
  struct Arena arena = arena_over_memory(request->bytes);
  (void)arena_alloc(&arena, request->count, request->size, 1);
}

/**
 * @brief Allocates one byte more than a 1 MiB region split from a reservation holds. Run through
 *        `test_is_oom`.
 *
 * @param arg Unused.
 */
static void alloc_past_split_region(void* arg) {
  (void)arg;
  struct Arena block;
  if (arena_reserve(RESERVE_LEN, &block) != 0) {
    return;
  }
  struct Arena region = arena_split(&block, 1024 * 1024);
  (void)arena_extend(&region, 1024 * 1024 + 1, 1);
}

#if SHARED_ASAN
/**
 * @brief Reports whether a child process that writes one byte past an arena allocation dies.
 *
 * The child's standard error goes to `/dev/null`, so the sanitizer's report does not clutter the
 * test output.
 *
 * @return `true` when the child ended any way other than exiting with status `0`.
 */
static bool overflow_past_allocation_dies(void) {
  (void)fflush(stdout);
  (void)fflush(stderr);
  const pid_t pid = fork();
  if (pid < 0) {
    return false;
  }
  if (pid == 0) {
    const int null_fd = open("/dev/null", O_WRONLY);
    if (null_fd >= 0) {
      (void)dup2(null_fd, STDERR_FILENO);
    }
    struct Arena block;
    if (arena_reserve(RESERVE_LEN, &block) != 0) {
      _Exit(0);
    }
    volatile char* bytes = arena_new(&block, 10, char);
    bytes[10] = 'x';
    _Exit(0);
  }
  int status = 0;
  if (waitpid(pid, &status, 0) != pid) {
    return false;
  }
  return !WIFEXITED(status) || WEXITSTATUS(status) != 0;
}
#endif

/**
 * @brief Reserves a size still over the reservation limit after every halving. Run through
 *        `test_is_oom`.
 *
 * @param arg Unused.
 */
static void reserve_halving_refused(void* arg) {
  (void)arg;
  arena_reserve_len_max = RESERVE_LEN;
  struct Arena block;
  (void)arena_reserve_halving((ptrdiff_t)RESERVE_LEN << (ARENA_RESERVE_HALVING_COUNT_MAX + 1),
                              &block);
}

// Allocations come back zeroed and distinct, one after the other in the region.
static void test_allocations_are_zeroed_and_distinct(void) {
  struct Arena arena = arena_over_memory(MEMORY_LEN);
  unsigned char* first = arena_new(&arena, 16, unsigned char);
  unsigned char* second = arena_new(&arena, 16, unsigned char);
  TEST_CHECK(second == first + 16);
  for (size_t i = 0; i < 16; i++) {
    TEST_CHECK(first[i] == 0);
    TEST_CHECK(second[i] == 0);
  }
  TEST_CHECK(arena.beg == memory + 32);
}

// A typed allocation after an odd-sized one is padded up to the type's alignment.
static void test_allocation_is_padded_to_alignment(void) {
  struct Arena arena = arena_over_memory(MEMORY_LEN);
  (void)arena_new(&arena, 1, char);
  int64_t* value = arena_new(&arena, 1, int64_t);
  TEST_CHECK(is_aligned(value, _Alignof(int64_t)));
  TEST_CHECK((char*)value == memory + _Alignof(int64_t));
  TEST_CHECK(*value == 0);
}

// A zero count returns a usable pointer and consumes only the padding.
static void test_zero_count_consumes_only_padding(void) {
  struct Arena arena = arena_over_memory(MEMORY_LEN);
  (void)arena_new(&arena, 1, char);
  void* empty = arena_new(&arena, 0, int64_t);
  TEST_CHECK(empty == memory + _Alignof(int64_t));
  TEST_CHECK(arena.beg == memory + _Alignof(int64_t));
}

// A copy of an arena allocates past the original's free-space start, and the original keeps that
// start, so whatever a by-value callee allocates is reclaimed when it returns.
static void test_copy_reclaims_on_return(void) {
  struct Arena arena = arena_over_memory(MEMORY_LEN);
  struct Arena scratch = arena;
  char* temporary = arena_new(&scratch, 100, char);
  TEST_CHECK(temporary == memory);
  TEST_CHECK(arena.beg == memory);
  TEST_CHECK(arena_new(&arena, 1, char) == temporary);
}

// One byte short of the free space fits. The check refuses a request that would fill it exactly, so
// that is the boundary: `MEMORY_LEN - 1` bytes is the largest request.
static void test_largest_request_is_one_short_of_the_free_space(void) {
  struct Arena arena = arena_over_memory(MEMORY_LEN);
  char* bytes = arena_new(&arena, MEMORY_LEN - 1, char);
  TEST_CHECK(bytes == memory);
  TEST_CHECK(arena.beg == memory + MEMORY_LEN - 1);

  struct AllocRequest exact = {.bytes = MEMORY_LEN, .count = MEMORY_LEN, .size = 1};
  TEST_CHECK(test_is_oom(alloc_request, &exact));
  // The boundary scales with the object size: seven 8-byte objects fit in 64 bytes, and eight do
  // not.
  struct AllocRequest fits = {.bytes = 64, .count = 7, .size = 8};
  TEST_CHECK(!test_is_oom(alloc_request, &fits));
  struct AllocRequest fills = {.bytes = 64, .count = 8, .size = 8};
  TEST_CHECK(test_is_oom(alloc_request, &fills));
}

// `arena_grow` extends the most recent allocation in place, doubling it, and keeps its elements.
static void test_grow_extends_last_allocation_in_place(void) {
  struct Arena arena = arena_over_memory(MEMORY_LEN);
  ptrdiff_t capacity = 0;
  int32_t* items = arena_grow(&arena, NULL, &capacity, sizeof(int32_t), _Alignof(int32_t));
  TEST_CHECK(capacity == 8);
  items[0] = 7;
  items[7] = 9;
  int32_t* grown = arena_grow(&arena, items, &capacity, sizeof(int32_t), _Alignof(int32_t));
  TEST_CHECK(grown == items);
  TEST_CHECK(capacity == 16);
  TEST_CHECK(grown[0] == 7 && grown[7] == 9 && grown[8] == 0 && grown[15] == 0);
  TEST_CHECK(arena.beg == (char*)(grown + 16));
}

// `arena_grow` moves an array that is no longer the most recent allocation, keeping its elements,
// and leaves the old copy readable.
static void test_grow_moves_an_array_that_is_not_last(void) {
  struct Arena arena = arena_over_memory(MEMORY_LEN);
  ptrdiff_t capacity = 0;
  int32_t* items = arena_grow(&arena, NULL, &capacity, sizeof(int32_t), _Alignof(int32_t));
  items[0] = 7;
  (void)arena_new(&arena, 1, char);
  int32_t* grown = arena_grow(&arena, items, &capacity, sizeof(int32_t), _Alignof(int32_t));
  TEST_CHECK(grown != items);
  TEST_CHECK(capacity == 16);
  TEST_CHECK(grown[0] == 7 && grown[15] == 0);
  TEST_CHECK(items[0] == 7);
}

// A reserved region has nothing usable until an allocation needs it, and an allocation past the
// usable part makes enough of the region usable for itself, so every byte of it can be written.
static void test_reserved_region_becomes_usable_on_demand(void) {
  struct Arena block;
  TEST_ASSERT(arena_reserve(RESERVE_LEN, &block) == 0);
  TEST_CHECK(block.end == block.beg);
  TEST_CHECK(block.limit - block.beg >= RESERVE_LEN);
  struct Arena arena = block;
  const ptrdiff_t len = 3 * 1024 * 1024;
  char* bytes = arena_new(&arena, len, char);
  TEST_CHECK(bytes[0] == 0 && bytes[len - 1] == 0);
  bytes[0] = 'a';
  bytes[len - 1] = 'z';
  TEST_CHECK(arena.end > arena.beg && arena.end <= arena.limit);
  arena_release(block);
}

// A copy that makes pages usable does not tell the original, which makes the same pages usable
// again when it reaches them and gets zeroed, writable storage at the same address.
static void test_original_reuses_pages_a_copy_made_usable(void) {
  struct Arena block;
  TEST_ASSERT(arena_reserve(RESERVE_LEN, &block) == 0);
  struct Arena arena = block;
  struct Arena scratch = arena;
  char* temporary = arena_new(&scratch, 2 * 1024 * 1024, char);
  memset(temporary, 0xA5, 2 * 1024 * 1024);
  TEST_CHECK(arena.end == arena.beg);
  char* kept = arena_new(&arena, 2 * 1024 * 1024, char);
  TEST_CHECK(kept == temporary);
  TEST_CHECK(kept[0] == 0 && kept[2 * 1024 * 1024 - 1] == 0);
  kept[2 * 1024 * 1024 - 1] = 'z';
  arena_release(block);
}

// A reservation the system grants keeps its full size.
static void test_reserve_halving_keeps_granted_size(void) {
  struct Arena block;
  TEST_CHECK(arena_reserve_halving(RESERVE_LEN, &block) == 0);
  TEST_CHECK(block.limit - block.beg == RESERVE_LEN);
  arena_release(block);
}

// Under a limit, each refused size is halved, and the first size that fits is halved once more:
// four and two times the limit are refused, the limit fits, and half of it is reserved.
static void test_reserve_halving_halves_refused_size(void) {
  arena_reserve_len_max = RESERVE_LEN;
  struct Arena block;
  TEST_CHECK(arena_reserve_halving(4 * RESERVE_LEN, &block) == 3);
  TEST_CHECK(block.limit - block.beg == RESERVE_LEN / 2);
  arena_release(block);
  arena_reserve_len_max = PTRDIFF_MAX;
}

// A split region ends where its share ends, rounded down to a multiple of 16, the parent's free
// space starts after it, and each makes its own pages usable.
static void test_split_regions_end_at_their_share(void) {
  struct Arena block;
  TEST_ASSERT(arena_reserve(RESERVE_LEN, &block) == 0);
  struct Arena rest = block;
  struct Arena first = arena_split(&rest, 1024 * 1024);
  TEST_CHECK(first.beg == block.beg);
  TEST_CHECK(first.limit == block.beg + 1024 * 1024);
  TEST_CHECK(rest.beg == first.limit && rest.limit == block.limit);
  const struct Arena odd = arena_split(&rest, 100);
  TEST_CHECK(odd.limit - odd.beg == 96 && rest.beg == odd.limit);
  // A block split into the shares it was sized from holds them, each rounded down too.
  struct Arena shares = arena_split(&rest, 1 + 14);
  TEST_CHECK(arena_split(&shares, 1).limit == shares.beg);
  TEST_CHECK(arena_split(&shares, 14).limit == shares.beg);
  char* in_first = arena_extend(&first, 1024 * 1024, 1);
  char* in_rest = arena_new(&rest, 16, char);
  in_first[1024 * 1024 - 1] = 'a';
  in_rest[0] = 'b';
  TEST_CHECK(in_first + 1024 * 1024 <= in_rest);
  TEST_CHECK(test_is_oom(alloc_past_split_region, NULL));
  arena_release(block);
}

// Splitting memory the caller owns gives a region usable from the start, with nothing to make
// usable, and a split longer than the free space ends in `os_oom`.
static void test_split_of_caller_memory_is_usable_at_once(void) {
  struct Arena arena = arena_over_memory(MEMORY_LEN);
  struct Arena region = arena_split(&arena, 100);
  TEST_CHECK(region.beg == memory && region.end == memory + 100 && region.limit == NULL);
  TEST_CHECK(arena.beg == memory + 100);
  TEST_CHECK(arena_new(&region, 99, char) == memory);
}

// `arena_extend` continues the last allocation with no padding, and fills the free space exactly.
static void test_extend_continues_last_allocation(void) {
  struct Arena arena = arena_over_memory(64);
  char* first = arena_new(&arena, 3, char);
  char* more = arena_extend(&arena, 61, 1);
  TEST_CHECK(more == first + 3);
  TEST_CHECK(arena.beg == arena.end);
}

// A discarded buffer reads as zero and stays writable, and the bytes on either side of it keep
// their values.
static void test_discard_zeroes_buffer_only(void) {
  struct Arena block;
  TEST_ASSERT(arena_reserve(RESERVE_LEN, &block) == 0);
  struct Arena arena = block;
  char* before = arena_new(&arena, 1, char);
  char* buffer = arena_new(&arena, DISCARD_LEN, char);
  char* after = arena_new(&arena, 1, char);
  *before = 'a';
  memset(buffer, 'b', DISCARD_LEN);
  *after = 'c';
  arena_discard(&arena, buffer, DISCARD_LEN);
  TEST_CHECK(buffer[DISCARD_LEN / 2] == 0);
  TEST_CHECK(*before == 'a');
  TEST_CHECK(*after == 'c');
  buffer[DISCARD_LEN / 2] = 'd';
  TEST_CHECK(buffer[DISCARD_LEN / 2] == 'd');
  arena_release(block);
}

// An arena over memory the caller owns is left alone, because the pages around it are not the
// arena's to map.
static void test_discard_leaves_caller_memory(void) {
  static char storage[DISCARD_LEN];
  memset(storage, 'x', sizeof(storage));
  const struct Arena arena = {.beg = storage, .end = storage + sizeof(storage)};
  arena_discard(&arena, storage, (ptrdiff_t)sizeof(storage));
  TEST_CHECK(storage[DISCARD_LEN / 2] == 'x');
}

#if SHARED_ASAN
// Under the address sanitizer the byte after an allocation is poisoned, so is the byte before the
// next one, and a buffer growing in place unpoisons exactly what it grows. A write past the
// allocation kills the process.
static void test_overflow_past_allocation_is_caught(void) {
  struct Arena block;
  TEST_ASSERT(arena_reserve(RESERVE_LEN, &block) == 0);
  struct Arena arena = block;
  char* first = arena_new(&arena, 10, char);
  char* second = arena_new(&arena, 10, char);
  TEST_CHECK(!__asan_address_is_poisoned(first + 9));
  TEST_CHECK(__asan_address_is_poisoned(first + 10));
  TEST_CHECK(__asan_address_is_poisoned(second - 1));
  TEST_CHECK(__asan_address_is_poisoned(second + 10));
  TEST_CHECK(__asan_region_is_poisoned(arena.beg, (size_t)(arena.end - arena.beg)) != NULL);

  struct Buf buffer = {.arena = &arena};
  for (int i = 0; i < 100; i++) {
    buf_append_char(&buffer, 'x');
  }
  TEST_CHECK(__asan_region_is_poisoned(buffer.data, (size_t)buffer.cap) == NULL);
  TEST_CHECK(__asan_address_is_poisoned(buffer.data + buffer.cap));

  TEST_CHECK(overflow_past_allocation_dies());
  arena_release(block);
}

// Under the address sanitizer releasing a region clears its poison, which the arena relies on so
// that a later mapping at the same addresses inherits none.
static void test_release_leaves_no_poison(void) {
  struct Arena block;
  TEST_ASSERT(arena_reserve(RESERVE_LEN, &block) == 0);
  struct Arena arena = block;
  (void)arena_new(&arena, 10, char);
  TEST_CHECK(__asan_region_is_poisoned(block.beg, RESERVE_LEN) != NULL);
  arena_release(block);
  TEST_CHECK(__asan_region_is_poisoned(block.beg, RESERVE_LEN) == NULL);
}
#endif

// A request past the free space ends in `os_oom`.
static void test_request_past_free_space_is_oom(void) {
  struct AllocRequest request = {.bytes = 16, .count = 17, .size = 1};
  TEST_CHECK(test_is_oom(alloc_request, &request));
}

// Counts whose byte total would overflow `ptrdiff_t` end in `os_oom` rather than wrapping to a
// small allocation. The second product wraps to exactly 0.
static void test_overflowing_count_is_oom(void) {
  struct AllocRequest largest = {.bytes = MEMORY_LEN, .count = PTRDIFF_MAX, .size = 1};
  TEST_CHECK(test_is_oom(alloc_request, &largest));
  struct AllocRequest wraps = {.bytes = MEMORY_LEN, .count = PTRDIFF_MAX / 2 + 1, .size = 2};
  TEST_CHECK(test_is_oom(alloc_request, &wraps));
}

// An exhausted arena refuses even a zero-count request, because the check compares against a
// quotient of zero.
static void test_empty_arena_refuses_any_request(void) {
  struct AllocRequest request = {.bytes = 0, .count = 0, .size = 1};
  TEST_CHECK(test_is_oom(alloc_request, &request));
}

// A size the system refuses at every halving ends in `os_oom`.
static void test_reserve_halving_refused_at_every_size_is_oom(void) {
  TEST_CHECK(test_is_oom(reserve_halving_refused, NULL));
}

TEST_LIST = {
    {"allocations are zeroed and distinct", test_allocations_are_zeroed_and_distinct},
    {"allocation is padded to alignment", test_allocation_is_padded_to_alignment},
    {"zero count consumes only padding", test_zero_count_consumes_only_padding},
    {"copy reclaims on return", test_copy_reclaims_on_return},
    {"largest request is one short of the free space",
     test_largest_request_is_one_short_of_the_free_space},
    {"grow extends last allocation in place", test_grow_extends_last_allocation_in_place},
    {"grow moves an array that is not last", test_grow_moves_an_array_that_is_not_last},
    {"reserved region becomes usable on demand", test_reserved_region_becomes_usable_on_demand},
    {"original reuses pages a copy made usable", test_original_reuses_pages_a_copy_made_usable},
    {"reserve halving keeps granted size", test_reserve_halving_keeps_granted_size},
    {"reserve halving halves refused size", test_reserve_halving_halves_refused_size},
    {"split regions end at their share", test_split_regions_end_at_their_share},
    {"split of caller memory is usable at once", test_split_of_caller_memory_is_usable_at_once},
    {"extend continues last allocation", test_extend_continues_last_allocation},
    {"discard zeroes buffer only", test_discard_zeroes_buffer_only},
    {"discard leaves caller memory", test_discard_leaves_caller_memory},
#if SHARED_ASAN
    {"overflow past allocation is caught", test_overflow_past_allocation_is_caught},
    {"release leaves no poison", test_release_leaves_no_poison},
#endif
    {"request past free space is oom", test_request_past_free_space_is_oom},
    {"overflowing count is oom", test_overflowing_count_is_oom},
    {"empty arena refuses any request", test_empty_arena_refuses_any_request},
    {"reserve halving refused at every size is oom",
     test_reserve_halving_refused_at_every_size_is_oom},
    {NULL, NULL},
};
