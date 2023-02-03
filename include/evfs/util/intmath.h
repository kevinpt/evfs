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

#ifndef INTMATH_H
#define INTMATH_H

typedef struct {
  int16_t x;
  int16_t y;
} Point16;


#ifdef __cplusplus
extern "C" {
#endif

uint32_t ceil_pow2(uint32_t x);
uint32_t floor_pow2(uint32_t x);

int32_t log2_fixed(uint32_t n, unsigned fp_exp);

unsigned ilog10(uint32_t n);

/*
Get base-10 digits in representation of a number

Args:
  n: Value to get digits for 

Returns:
  Number of digits
*/
static inline unsigned base10_digits(uint32_t n) {
  if(n == 0)
    return 1;
  else
    return ilog10(n) + 1;
}

unsigned ilog_b(unsigned n, unsigned base);


/*
Absolute value of uint8_t

Args:
  n:    Value to get absolute value of

Returns:
  Absolute value
*/
static inline int8_t iabs_8(int8_t n) {
  return n < 0 ? -n : n;
}


/*
Absolute value of short

Args:
  n:    Value to get absolute value of

Returns:
  Absolute value
*/
static inline short iabs_s(short n) {
  return n < 0 ? -n : n;
}

// C11 is required for generics
#if defined __STDC_VERSION__ && __STDC_VERSION__ >= 201112L

#  define iabs(n)  _Generic((n), \
      int8_t : iabs_8, \
      short  : iabs_s, \
      int    : abs, \
      long   : labs  \
    )(n)

#endif

unsigned long isqrt_fixed(unsigned long fp_value, unsigned fp_exp);

unsigned ufixed_to_uint(unsigned fp_value, unsigned fp_scale);
int fixed_to_int(int fp_value, unsigned fp_scale);

int to_fixed_base10_parts(long value, unsigned long fp_scale, long *integer, long *frac);
int fixed_base10_adjust(long *integer, long *frac, int frac_digits, int frac_places);
long to_fixed_base10(long value, unsigned long fp_scale, int frac_places, int *b10_exp);
long to_fixed_si(long value, int value_exp, unsigned fp_scale, char *si_prefix, bool pow2);

Point16 interpolate_points(Point16 p0, Point16 p1, uint16_t t);
uint16_t quadratic_solve(int32_t a, int32_t b, int32_t c, int16_t x);
uint16_t bezier_solve_t(int16_t x0, int16_t x1, int16_t x2, int16_t x);
int16_t quadratic_eval(int16_t a, int16_t b, int16_t c, uint16_t t);

Point16 quadratic_bezier(Point16 p0, Point16 p1, Point16 p2, uint16_t t);
uint16_t bezier_search_t(Point16 p0, Point16 p1, Point16 p2, int16_t x);

#ifdef __cplusplus
}
#endif

#endif // INTMATH_H
