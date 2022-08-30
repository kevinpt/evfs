/* SPDX-License-Identifier: MIT
Copyright 2021 Kevin Thibedeau
(kevin 'period' thibedeau 'at' gmail 'punto' com)

Permission is hereby granted, free of charge, to any person obtaining a
copy of this software and associated documentation files (the "Software"),
to deal in the Software without restriction, including without limitation
the rights to use, copy, modify, merge, publish, distribute, sublicense,
and/or sell copies of the Software, and to permit persons to whom the
Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice (including the next
paragraph) shall be included in all copies or substantial portions of the
Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
DEALINGS IN THE SOFTWARE.
*/

#include <stdint.h>
#include <stdbool.h>

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

uint32_t ceil_pow2(uint32_t x) {
  return 1ul << (32 - clz(x - 1));
}

uint32_t floor_pow2(uint32_t x) {
  return 1ul << (31 - clz(x));
}


/*
Integer base-10 logarithm

From Hacker's Delight 2nd ed. Fig 11-10

Args:
  n: Input to logarithm

Returns:
  floor(log10(n))
*/
unsigned ilog10(uint32_t n) {
  unsigned log;

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


/*
Integer logarithm with arbitrary base

Args:
  n:    Input to logarithm
  base: Base for the logarithm

Returns:
  floor(log_base_(n))
*/
unsigned ilog_b(unsigned n, unsigned base) {
  unsigned log, residual;

  residual = n;
  log = 0;

  while(residual > base-1) {
    residual = residual / base;
    log++;
  }

  return log;
}


/*
Convert fixed point value to integer

Args:
  fp_value: Value in fixed point representation
  fp_scale: Scale factor for fp_value

Returns:
  Integral part of fp_value with rounded up fraction removed
*/
unsigned ufixed_to_uint(unsigned fp_value, unsigned fp_scale) {
  fp_value += fp_scale/2;  // Round up
  return fp_value / fp_scale;
}


/*
Convert signed fixed point value to integer

Args:
  fp_value: Value in fixed point representation
  fp_scale: Scale factor for fp_value

Returns:
  Integral part of fp_value with rounded up fraction removed
*/
int fixed_to_int(int fp_value, unsigned fp_scale) {
  int half = (int)fp_scale/2;
  fp_value += (fp_value < 0) ? -half : half; // Round to +/- infinity
  return fp_value / (int)fp_scale;
}



/*
Convert a fixed point number in any base to integer and fractional parts in base-10

Args:
  value:    Fixed point value
  fp_scale: Scale factor for value
  integer:  Integer portion of value
  frac:     Base-10 fraction from value

Returns:
  Base-10 exponent for the converted number
*/
int to_fixed_base10_parts(long value, unsigned long fp_scale, long *integer, long *frac) {
  if(fp_scale <= 1) {
    *integer  = value;
    *frac     = 0;
    return 0;
  }

  long vi = value / (long)fp_scale;
  int vf = value % (long)fp_scale;

  if(vf < 0)  vf = -vf; // Remove sign on fraction to simplify rounding

  // If fp_scale is already a power of 10 we don't want to grow by an extra digit so
  // decrement when taking log.
  unsigned frac_digits = base10_digits(fp_scale-1);

  // get 10 ^ base10_digits(fp_scale)
  int scale_b10 = 1;
  for(unsigned i = 0; i < frac_digits; i++) {
    scale_b10 *= 10;
  }

  // Convert fraction to base-10 rounded to +/- infinity
  vf = ((long long)vf * scale_b10 + fp_scale/2) / (long)fp_scale;

  if(value < 0) vf = -vf; // Restore sign on fraction
  *integer  = vi;
  *frac     = vf;
  return frac_digits;
}


/*
Adjust scaling of a fixed base-10 number generated by :c:func:`to_fixed_base10_parts`

This rounds to +/- infinity if frac_places is less than frac_digits

Args:
  integer:      Integer portion of value
  frac:         Base-10 fraction from value
  frac_digits:  Base-10 exponent of integer.frac number
  frac_places:  Requested number of fractional digits in result, -1 for no-op

Returns:
  Base-10 exponent for the adjusted number. Adjusted value with rounding in integer and frac.
*/
int fixed_base10_adjust(long *integer, long *frac, int frac_digits, int frac_places) {
  long vi = *integer;
  long vf = *frac;

  if(vf < 0)  vf = -vf; // Remove sign on fraction to simplify rounding

  // Convert exponent to scaling
  long scale_b10 = 1;
  for(int i = 0; i < frac_digits; i++) {
    scale_b10 *= 10;
  }

  // Adjust fractional places
  if(frac_places > 0) {
    // Reduce digits if fraction is larger than requested decimal places
    while(frac_digits > frac_places+1) { // Initial scale down without rounding
      vf /= 10;
      frac_digits--;
    }

    if(frac_digits > frac_places) { // Final adjust with rounding
      vf += 5;  // Start round to +/- infinity in last decimal place

      if(vf > scale_b10) { // Rounded into integer
        vi++;
        vf = 0; // No fraction part
        frac_digits = 0;

      } else {  // Finish last adjustment
        vf /= 10;
        frac_digits--;
      }
    }

  } else if(frac_places == 0) { // No fraction part
    // Check for rounding into integer
    if(vf + scale_b10/2 >= scale_b10)  // Round up in 1/10ths place
      vi += (vi < 0 ? -1 : 1);  // Overflow into integer

    vf = 0;
    frac_digits = 0;
  }

  *integer = vi;

  if(vi < 0) vf = -vf; // Restore sign on fraction
  *frac = vf;
  return frac_digits;
}


/*
Convert a fixed point number in any base to fixed point with a base-10 exponent

Args:
  value:        Fixed point value
  fp_scale:     Scale factor for value
  frac_places:  Requested number of fractional digits in result, -1 for max precision
  b10_exp:      Base-10 exponent of the result

Returns:
  value converted to base-10 scaling
*/
long to_fixed_base10(long value, unsigned long fp_scale, int frac_places, int *b10_exp) {
  long integer;
  long frac;

  int frac_digits = to_fixed_base10_parts(value, fp_scale, &integer, &frac);
  if(frac_places >= 0)  // Rescale
    frac_digits = fixed_base10_adjust(&integer, &frac, frac_digits, frac_places);

  // Convert exponent to scaling
  int scale_b10 = 1;
  for(int i = 0; i < frac_digits; i++) {
    scale_b10 *= 10;
  }

  *b10_exp = -frac_digits;
  return integer * scale_b10 + frac;
}



/*
Convert an integer to fixed point reduced by the appropriate SI power

The pow2 scaling option is only available when value exponent is 0.

Args:
  value:        Integer value
  value_exp:    Base-10 exponent for value
  fp_scale:     Fixed-point scale factor for result
  si_prefix:    Prefix character of scaled result, ' ' for x10^0
  pow2:         Use "computer" units scaled by 1024 instead of 1000

Returns:
  value converted to fixed-point in range of [0.0 - 1000.0) scaled by fp_scale.
  Prefix in si_prefix.
*/
long to_fixed_si(long value, int value_exp, unsigned fp_scale, char *si_prefix, bool pow2) {
  static const char s_si_prefix[]     = "afpnum kMGTPE";
#define PREFIX_10_0  6    // Offset of x10^0 space char

  const unsigned long si_pow = (value_exp == 0) ? (pow2 ? 1024 : 1000) : 1000;

  const char *prefix_pos;

  int value_exp_si = (value_exp / 3) * 3; // Pick nearest prefix below the value exponent
  // Need to prescale by 10x or 100x if exponent not already a multiple of 3
  for(int i = 0; i < (value_exp - value_exp_si); i++) {
    value *= 10;
  }

  // Set initial prefix to match that of value ULP
  prefix_pos = &s_si_prefix[PREFIX_10_0 + (value_exp_si / 3)];

  bool negative = value < 0;
  if(negative) value = -value;  // Force positive to simplify prefix selection

  // Find the correct prefix
  unsigned long min_val = 1;
  unsigned long max_val;
  while(*prefix_pos != '\0') {
    max_val = min_val * si_pow; // Next thousands

    if((unsigned long)value < max_val || prefix_pos[1] == '\0')
      break;

    prefix_pos++;
    min_val = max_val;
  }

  *si_prefix = (*prefix_pos == ' ') ? '\0' : *prefix_pos;

  // Convert to fixed point with rounding to +/-infinity
  value = ((long long)value * fp_scale + min_val/2) / min_val;
  return negative ? -value : value;
}


/*
Calculate a fixed point squart root from a fixed-point value

The precision is controlled by the size of the fp_exp scale factor.
fp_value and the result are in base-2 fixed-point format.

Args:
  fp_value:     Fixed-point number to take root of
  fp_exp:       Base-2 exponent for fp_value. Must be even

Returns:
  Square root of fp_value with the same fixed-point scaling
*/
unsigned long isqrt_fixed(unsigned long fp_value, unsigned fp_exp) {
  /* Reference:
    Adapted from Christophe Meessen's fixed point sqrt() implementation:
    https://github.com/chmike/fpsqrt
    https://groups.google.com/forum/?hl=fr%05aacf5997b615c37&fromgroups#!topic/comp.lang.c/IpwKbw0MAxw/discussion
  */
//  const unsigned short total_bits   = 8 * sizeof fp_value;
#define TOTAL_BITS  (8 * sizeof fp_value)
  const unsigned short integer_bits = TOTAL_BITS - fp_exp;

  const unsigned short adj_bits = integer_bits / 2; // Number of bits shifted for final adjustment
  const unsigned short aux_bits = adj_bits >= 2 ? 2 : adj_bits; // Additional aux bits

  if(integer_bits & 0x01) // Integer bits must be even
    return 0;

  unsigned long t, q, b, r;
  r = fp_value;
  q = 0;

  /*
    This algorithm computes sqrt(x) with an offset factor 2^(I+F):
      q = sqrt(X*2^F * 2^(I+F)) where x = X*2^F

    We want the fractional part of the offset in the result:
        = sqrt(x) * 2^F * sqrt(2^I)  =  sqrt(x) * 2^F * 2^(I/2)

    The remaining offset is shifted off for the final correction.
    Because these bits are being removed we can terminate the loop early
    to reduce the iterations.
  */

  unsigned long end_b = 1UL << (adj_bits - aux_bits);
  for(b = 1UL << (TOTAL_BITS-2); b > end_b; b >>= 1) {
    t = q + b;
    if(r >= t) {
      r -= t;
      q = t + b; // Equivalent to q += 2*b
    }
    r <<= 1;
  }

  // Remove remaining offset factor and round up
  q = (q >> adj_bits) + (unsigned long)(r > q);
  return q;
}

