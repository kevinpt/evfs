/* SPDX-License-Identifier: MIT
Copyright 2020 Kevin Thibedeau
(kevin 'period' thibedeau 'at' gmail 'punto' com)

See LICENSE in the EVFS project root for details
*/

#ifndef EVFS_INTERNAL_H
#define EVFS_INTERNAL_H

#include <stdarg.h>

#ifdef EVFS_USE_THREADING
#  include "evfs_custom_threading.h"
#endif

// Demons be gone! ... Now this is safe, right
#define MIN(a, b)   ((a) < (b) ? (a) : (b))
#define MAX(a, b)   ((a) > (b) ? (a) : (b))


// ******************** Debug support ********************

#ifndef EVFS_DEBUG
#  if !defined NDEBUG
#    define EVFS_DEBUG 1
#  else
#    define EVFS_DEBUG 0
#  endif
#endif

void evfs__err_printf(const char *fname, int line, const char *msg, ...);
void evfs__dbg_printf(const char *msg, ...);


// ANSI color macros for EVFS_USE_ANSI_COLOR
#define A_BLK "\e[0;30m"
#define A_RED "\e[0;31m"
#define A_GRN "\e[0;32m"
#define A_YLW "\e[0;33m"
#define A_BLU "\e[0;34m"
#define A_MAG "\e[0;35m"
#define A_CYN "\e[0;36m"
#define A_WHT "\e[0;37m"

#define A_BBLK "\e[1;30m"
#define A_BRED "\e[1;31m"
#define A_BGRN "\e[1;32m"
#define A_BYLW "\e[1;33m"
#define A_BBLU "\e[1;34m"
#define A_BMAG "\e[1;35m"
#define A_BCYN "\e[1;36m"
#define A_BWHT "\e[1;37m"

#define A_NONE "\e[0m"

/* All of these debug assertions return true when their test condition fails.
   They are meant to be strung together in an if statament that performs error handling.

    USAGE:

    if(ASSERT(a == b, "fail message: %d != %d", a, b)) {
      <handle error>;
    }

    // Fail when pointers are NULL
    if(PTR_CHECK(x) || (PTR_CHECK(y)) <handle error>;

    // Fail when alloc fails. Can't be disabled like ASSERT() and PTR_CHECK().
    buf = evfs_malloc(size);
    if(MEM_CHECK(buf)) <handle error>;
*/

// We want optional variadic args without using the GCC extension.
// This wrapper with an injected final arg lets us do that.
// See: https://stackoverflow.com/a/53875012/1583598
#define va_evfs_assert(expr, msg, ...) evfs__err_printf(__FILE__, __LINE__, "\"" #expr "\" | " msg "%c", __VA_ARGS__)

// Use EVFS_ASSERT_LEVEL to control debug reporting level for ASSERT() macro:
//   0:   Always return 0 (assertion ignored)
//   1:   Silent assertion check
//   2:   Check assertion with diagnostic output
//   3:   Check assertion with diagnostic output and abort() when EVFS_DEBUG == 1
#if EVFS_ASSERT_LEVEL >= 3
#  if EVFS_DEBUG == 1
#    define ASSERT(expr, ...) ((expr) ? 0 : (va_evfs_assert(expr, __VA_ARGS__, '\n'), abort(), 1))
#  else
#    define ASSERT(expr, ...) !(expr)
#  endif
#elif EVFS_ASSERT_LEVEL >= 2
#  define ASSERT(expr, ...) ((expr) ? 0 : (va_evfs_assert(expr, __VA_ARGS__, '\n'), 1))
#elif EVFS_ASSERT_LEVEL >= 1
#  define ASSERT(expr, ...) !(expr)
#else
#  define ASSERT(expr, ...) 0
#endif

#define PTR_CHECK(ptr) ASSERT(ptr, "NULL value")

#define MEM_CHECK(ptr) ((ptr) ? 0 : (va_evfs_assert(ptr, "malloc failure", '\n'), 1))

/* THROW() return macro for tracking error results
   USAGE:
      if(error_condition)
        THROW(error_code);
*/
#if EVFS_DEBUG == 1
#  define THROW(code) do {evfs__err_printf(__FILE__, __LINE__, #code " (%d)\n", (code)); return (code);} while(0)
#else
#  define THROW(code) return (code)
#endif

// General purpose debug messages. First argument should be a format string
#ifdef EVFS_USE_ANSI_COLOR
#  define va_evfs_debug_print(msg, ...)  evfs__dbg_printf(A_BGRN msg A_NONE "%c", __VA_ARGS__)
#else
#  define va_evfs_debug_print(msg, ...)  evfs__dbg_printf(msg "%c", __VA_ARGS__)
#endif

#if EVFS_DEBUG == 1
#  define DPRINT(...)  va_evfs_debug_print(__VA_ARGS__, '\n')
#else
#  define DPRINT(...)
#endif



// ******************** Locking support ********************


#if defined EVFS_SOURCE_USES_LOCK && defined EVFS_USE_THREADING

// You neeed to provide an implementation of these functions on platforms
// without pthreads.
int evfs__lock_init(EvfsLock *lock);
int evfs__lock_destroy(EvfsLock *lock);
int evfs__lock(EvfsLock *lock);
int evfs__unlock(EvfsLock *lock);

void evfs__init_once(void); // Must be provided by threading wrapper
#else // Disable locking API

typedef unsigned EvfsLock;

static inline int evfs__nop(EvfsLock *(l)) { return 0; }

#define evfs__lock_init(l)     evfs__nop(l)
#define evfs__lock_destroy(l)  evfs__nop(l)
#define evfs__lock(l)          evfs__nop(l)
#define evfs__unlock(l)        evfs__nop(l)

#endif


void evfs__lib_init(void);  // Called by evfs__init_once()

bool evfs__vfs_existing_dir(Evfs *vfs, const char *path);
evfs_off_t evfs__absolute_offset(EvfsFile *fh, evfs_off_t offset, EvfsSeekDir origin);
char *evfs__vmprintf(const char *fmt, va_list args);

#endif // EVFS_INTERNAL_H

