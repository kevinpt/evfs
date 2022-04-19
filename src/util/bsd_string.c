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

#include <string.h>
#include "bsd/string.h"

/*
Copy a string with length limit

src must be NUL terminated
Truncation happened if strlcpy(...) >= size

Args:
  dst:  Copy destination
  src:  Copy source
  size: Max bytes to copy including NUL

Returns:
  Number of bytes in src
*/
size_t strlcpy(char *dst, const char *src, size_t size) {
  size_t src_len = 0;

  if(size > 0) {
    size--; // Leave room for NUL
    while(*src != '\0' && src_len < size) {
      *dst++ = *src++;
      src_len++;
    }

    *dst = '\0';
  }

  while(*src != '\0') { // Count remaining chars in src
    src++;
    src_len++;
  }

  return src_len;
}


/*
Copy a string with length limit

Similar to strlcpy() but src does not need to be NUL terminated.
This allows copying substrings from a larger string.

Truncation detection requires a strlen() call:
   strxcpy(...) < strlen(src)

Args:
  dst:  Copy destination
  src:  Copy source
  size: Max bytes to copy including NUL

Returns:
  Number of chars copied to dst not including NUL
*/
size_t strxcpy(char *dst, const char *src, size_t size) {
  size_t copied = 0;

  if(size > 0) {
    size--;
    while(*src != '\0' && copied < size) {
      *dst++ = *src++;
      copied++;
    }

    *dst = '\0';
  }

  return copied;
}


/*
Concatenate strings with length limit

src must be NUL terminated
Truncation happened if strlcat(...) >= size

Args:
  dst:  Copy destination
  src:  Copy source
  size: Max bytes to copy including NUL

Returns:
  Number of bytes in total string
*/
size_t strlcat(char *dst, const char *src, size_t size) {
  size_t cat_len = 0;

  if(size > 0) {
    while(*dst != '\0' && cat_len < size) {
      dst++;
      cat_len++;
    }

    if(cat_len >= size) // No NUL in dst
      cat_len += strlen(src);
    else
      cat_len += strlcpy(dst, src, size - cat_len);
  }

  return cat_len;
}

