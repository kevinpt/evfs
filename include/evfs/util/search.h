#ifndef _SEARCH_H_
#define _SEARCH_H_

size_t search_nearest(const void *key, const void *base, size_t num, size_t item_size,
                       ptrdiff_t (*compare_near)(const void *pkey,const void *pelem));

size_t search_nearest_above(const void *key, const void *base, size_t num, size_t item_size,
                       ptrdiff_t (*compare_near)(const void *pkey,const void *pelem));

size_t search_nearest_below(const void *key, const void *base, size_t num, size_t item_size,
                       ptrdiff_t (*compare_near)(const void *pkey,const void *pelem));



/*
Generate a binary search function with an inlinable comparison function.

This can avoid the function call overhead of the comparison function for faster
searches. You should declare the compare function as "static inline" to maximize
the likelihood that the compiler will actually inline it. -O2 may also be needed.

The generated function has the same prototype as bsearch() except the final
argument for the comparison callback is not present.

USAGE:

  static inline int my_compare(const void *key, const void *item) {
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

#endif // _SEARCH_H_
