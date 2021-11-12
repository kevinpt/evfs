#ifndef INTMATH_H
#define INTMATH_H

#ifdef __cplusplus
extern "C" {
#endif

int ilog10(uint32_t n);

// Get number of digits needed to represent n
static inline unsigned base10_digits(uint32_t n) {
  if(n == 0)
    return 1;
  else
    return ilog10(n) + 1;
}


#ifdef __cplusplus
}
#endif

#endif // INTMATH_H
