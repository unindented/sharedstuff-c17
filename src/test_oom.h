#ifndef SHARED_TEST_OOM_H
#define SHARED_TEST_OOM_H

// `fork` and `waitpid` are POSIX rather than C17, so a strict `-std=c17` compile hides them unless
// a feature-test macro asks for them. macOS needs `_DARWIN_C_SOURCE` as well, because its
// `<sys/wait.h>` uses BSD types that `_POSIX_C_SOURCE` alone hides. Both must precede every
// `#include`.
#define _DARWIN_C_SOURCE
#define _POSIX_C_SOURCE 200809L

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

// This header defines `os_oom` for one unit-test executable, so include it from exactly one
// translation unit.

/** Exit status the test build's `os_oom` ends the process with. Distinct from any crash. */
enum { TEST_OOM_EXIT_STATUS = 86 };

_Noreturn void os_oom(void) {
  _Exit(TEST_OOM_EXIT_STATUS);
}

/**
 * @brief Reports whether running `fn` ends in `os_oom`.
 *
 * Runs `fn` in a child process, because `os_oom` ends the process it runs in. A child that returns
 * from `fn`, crashes, or exits any other way does not count.
 *
 * @param fn  Function to run in the child. Must not be `NULL`.
 * @param arg Opaque argument forwarded to `fn`.
 * @return `true` when the child exited through `os_oom`, or `false` otherwise.
 */
static bool test_is_oom(void (*fn)(void*), void* arg) {
  // Flush first, so output buffered before the fork is not written once by each process.
  (void)fflush(stdout);
  (void)fflush(stderr);
  const pid_t pid = fork();
  if (pid < 0) {
    return false;
  }
  if (pid == 0) {
    fn(arg);
    _Exit(0);
  }
  int status = 0;
  if (waitpid(pid, &status, 0) != pid) {
    return false;
  }
  return WIFEXITED(status) && WEXITSTATUS(status) == TEST_OOM_EXIT_STATUS;
}

#endif
