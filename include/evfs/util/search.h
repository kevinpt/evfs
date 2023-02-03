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

/*
------------------------------------------------------------------------------
search

General purpose approximate search functions.
------------------------------------------------------------------------------
*/

#ifndef SEARCH_H
#define SEARCH_H

typedef ptrdiff_t (*CompareNearFunc)(const void *pkey,const void *pentry);

ptrdiff_t search_exact(const void *key, const void *base, size_t num, size_t item_size,
                       CompareNearFunc compare_near);

size_t search_nearest(const void *key, const void *base, size_t num, size_t item_size,
                       CompareNearFunc compare_near);

size_t search_nearest_above(const void *key, const void *base, size_t num, size_t item_size,
                       CompareNearFunc compare_near);

size_t search_nearest_below(const void *key, const void *base, size_t num, size_t item_size,
                       CompareNearFunc compare_near);



/*
Generate a binary search function with an inlinable comparison function.

This can avoid the function call overhead of the comparison function for faster
searches. You should declare the compare function as "static inline" to maximize
the likelihood that the compiler will actually inline it. -O2 may also be needed.

The generated function has the same prototype as bsearch() except the final
argument for the comparison callback is not present.

USAGE:

  static inline ptrdiff_t my_compare(const void *key, const void *item) {
    // Implement your item comparison
  }


  GEN_BSEARCH_FUNC(bsearch_fast, my_compare)

  ...

  int data[100] = {...};
  int key = 42;

  int *found = bsearch_fast(&key, data, COUNT_OF(data), sizeof(data[0]));
  if(found) printf("Key %d found in array", key);


You can also inline the search function if it will be called in a hot section
of code:

  static inline GEN_BSEARCH_FUNC(bsearch_fast, my_compare)

*/

#define GEN_BSEARCH_FUNC(fun_name, compare) \
  void *fun_name(const void *key, const void *base, size_t num, size_t item_size) { \
    ptrdiff_t low = 0; \
    ptrdiff_t high = num-1; \
\
    while(low <= high) { \
      ptrdiff_t mid = low + (high-low)/2; \
      int delta = compare(key, base + mid*item_size); \
      if(delta < 0) { \
        high = mid-1; \
      } else if(delta > 0) { \
        low = mid+1; \
      } else { \
        return (void *)(base + mid*item_size); \
      } \
    } \
\
    return NULL; \
  }

#endif // SEARCH_H
