#ifndef BSD_STRING_H
#define BSD_STRING_H

#ifdef __cplusplus
extern "C" {
#endif

// Newlib has strlcpy() and strlcat() so we inhibit a duplicate declaration
#ifdef linux
size_t strlcpy(char *dst, const char *src, size_t size);
size_t strlcat(char *dst, const char *src, size_t size);
#endif

size_t strxcpy(char *dst, const char *src, size_t size);

#define strlcpy_check(d, s, sz)  (strlcpy((d), (s), (sz)) < (sz) ? true : false)


#ifdef __cplusplus
}
#endif


#endif // BSD_STRING_H
