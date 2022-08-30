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

This uses a generalized binary search that requires the array to be sorted.

The search functions always return a valid index. If the key is before or after the
lowest or highest entries in the array then 0 or max index will be returned.


USAGE:

Define a comparison function of type CompareNearFunc that returns the magnitude
of the difference between the search key and a trial entry. Returns 0 for
equality. When array is sorted in ascending order, negative values indicate the key
will be found before the trial entry and positive values for a key after the trial.
This logic reverses if the array is sorted in descending order.


  ptrdiff_t compare(const void *pkey, const void *pentry) {
    int key = *(int *)pkey;
    int entry = *(int *)pentry;

    return key - entry;
  }

  int a[] = {10, 20, 30, 40};

  size_t find_key(int key) {
    return search_nearest(&key, a, 4, sizeof(a[0]), compare);
  }

------------------------------------------------------------------------------
*/

#include <stddef.h>
#include "search.h"

/*
Search an array for an exact matching item

Ths is similar to bsearch() but the compare_near() callback returns the magnitude
of difference between key and a candidate element. The callback returns the signed
difference between compared keys that indicates how close they are to each other
by any measure the caller wishes to use.

Args:
  key:          Item to search for
  base:         Start of array
  num:          Number of items in base array
  item_size:    Size of items in the array
  compare_near: Callback to compare items

Returns:
  Index into the array or -1 on failure
*/
ptrdiff_t search_exact(const void *key, const void *base, size_t num, size_t item_size,
                       CompareNearFunc compare_near) {

  // NOTE: high can drop below 0 so we need indices to be signed
  ptrdiff_t low = 0;
  ptrdiff_t high = num-1;

  // Normal binary search
  while(low <= high) {
    ptrdiff_t mid = low + (high-low)/2;

    ptrdiff_t delta = compare_near(key, (char *)base + mid*item_size);
    //printf(" | (%d) %d %d-%d-%d\n", *(int*)key, delta, low, mid, high);
    if(delta < 0) { // Key is below mid point
      high = mid-1;
    } else if(delta > 0) { // Key is above mid point
      low = mid+1;
    } else { // Exact match
      return mid;
    }
  }

  // Key not found.
  return -1;
}


/*
Search an array for the nearest item to the key

This is similar to bsearch() but the compare_near() callback returns the magnitude
of difference between key and a candidate element. The callback returns the signed
difference between compared keys that indicates how close they are to each other
by any measure the caller wishes to use.

Args:
  key:          Item to search for
  base:         Start of array
  num:          Number of items in base array
  item_size:    Size of items in the array
  compare_near: Callback to compare items

Returns:
  The nearest index in the array
*/
size_t search_nearest(const void *key, const void *base, size_t num, size_t item_size,
                       CompareNearFunc compare_near) {

  // NOTE: high can drop below 0 so we need indices to be signed
  ptrdiff_t low = 0;
  ptrdiff_t high = num-1;

  // Normal binary search
  while(low <= high) {
    ptrdiff_t mid = low + (high-low)/2;

    ptrdiff_t delta = compare_near(key, (char *)base + mid*item_size); 
    //printf(" | (%d) %d %d-%d-%d\n", *(int*)key, delta, low, mid, high);
    if(delta < 0) { // Key is below mid point
      high = mid-1;
    } else if(delta > 0) { // Key is above mid point
      low = mid+1;       
    } else { // Exact match
      return mid;
    }

  }

  // Key not found.
  // At this point low is one greater than high. Key would lie between them.

  // Bounds check if we went past the ends
  if(low >= (ptrdiff_t)num)
    return num-1;

  if(high < 0)
    return 0;

  // Find nearest
  ptrdiff_t hd = compare_near((char *)base + low*item_size, key);  // High delta: base[high] - key
  ptrdiff_t ld = compare_near(key, (char *)base + high*item_size); // Low delta:  key - base[low]
  //printf(" ## %d - %d, %d %d --> %d\n", low, high, ld, hd , (ld < hd) ? high : low);
  // If key is closer to lower index value we use its index (actually high) and vice versa
  return (ld < hd) ? high : low;
}


/*
Search an array for the nearest item greater than or equal to the key

Ths is similar to bsearch() but the compare_near() callback returns the magnitude of
difference between key and a candidate element.

If the search key is more than the largest item then the returned nearest index will
be below the key value.

Args:
  key:          Item to search for
  base:         Start of array
  num:          Number of items in base array
  item_size:    Size of items in the array
  compare_near: Callback to compare items

Returns:
  The nearest index in the array >= key
*/
size_t search_nearest_above(const void *key, const void *base, size_t num, size_t item_size,
                       CompareNearFunc compare_near) {

  size_t ix = search_nearest(key, base, num, item_size, compare_near);

  // Coerce up if not at end of array
  if(ix < num-1 && compare_near(key, (char *)base + ix*item_size) > 0)
    ix++;

  return ix;
}


/*
Search an array for the nearest item less than or equal to the key

Ths is similar to bsearch() but the compare_near() callback returns the magnitude of
difference between key and a candidate element.

If the search key is less than the smallest item then the returned nearest index will
be above the key value.

Args:
  key:          Item to search for
  base:         Start of array
  num:          Number of items in base array
  item_size:    Size of items in the array
  compare_near: Callback to compare items

Returns:
  The nearest index in the array <= key
*/
size_t search_nearest_below(const void *key, const void *base, size_t num, size_t item_size,
                       CompareNearFunc compare_near) {

  size_t ix = search_nearest(key, base, num, item_size, compare_near);

  // Coerce down if not at end of array
  if(ix > 0 && compare_near(key, (char *)base + ix*item_size) < 0)
    ix--;

  return ix;
}

