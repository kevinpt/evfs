#include <stdint.h>

#include "intmath.h"


// __builtin_clz() added in GCC 3.4 and Clang 5
#if (defined __GNUC__ && __GNUC__ >= 4) || (defined __clang__ && __clang_major__ >= 5)
#  define HAVE_BUILTIN_CLZ
#endif

#ifdef HAVE_BUILTIN_CLZ
#  define clz(x)  __builtin_clz(x)
#else
// Count leading zeros
// From Hacker's Delight 2nd ed. Fig 5-12. Modified to support 16-bit ints.
static int clz(unsigned x) {
  static_assert(sizeof(x) <= 4, "clz() only supports a 32-bit or 16-bit argument");
  unsigned y;
  int n = sizeof(x) * 8;

  if(sizeof(x) > 2) { // 32-bit x
    y = x >> 16; if(y) {n -= 16; x = y;}
  }
  y = x >> 8;  if(y) {n -= 8; x = y;}
  y = x >> 4;  if(y) {n -= 4; x = y;}
  y = x >> 2;  if(y) {n -= 2; x = y;}
  y = x >> 1;  if(y) return n - 2;

  return n - x;
}
#endif


// Floor(log10(n))
// From Hacker's Delight 2nd ed. Fig 11-10
int ilog10(uint32_t n) {
  int log;

  // Estimated base-10 log. Produces wrong result for some ranges of values.
  static const uint8_t log10_est[] = {
              // clz()    base-10 range
    9,9,9,    // 0 - 2    (2**32-1 - 2**29)
    8,8,8,    // 3 - 5    (2**29-1 - 2**26)
    7,7,7,    // 6 - 8    (2**26-1 - 2**23)
    6,6,6,6,  // 9 - 12   (2**23-1 - 2**19)
    5,5,5,    // 13 - 15  (2**19-1 - 65536)
    4,4,4,    // 16 - 18  (65535 - 8192)
    3,3,3,3,  // 19 - 22  (8191 - 512)
    2,2,2,    // 23 - 25  (511 - 64)
    1,1,1,    // 26 - 28  (63 - 8)
    0,0,0,0   // 29 - 32  (7 - 0)
  };

  // Thresholds for correcting initial estimate
  static const uint32_t pow10[] = {
    1, 10, 100, 1000, 10000, 100000,
    1000000, 10000000, 100000000, 1000000000
  };

  log = log10_est[clz(n)];
  if(n < pow10[log]) // Apply correction
    log--;

  return log;
}

