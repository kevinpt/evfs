/* SPDX-License-Identifier: MIT
Copyright 2020 Kevin Thibedeau
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

#ifndef RANGE_STRINGS_H
#define RANGE_STRINGS_H

#include <stdio.h> // For FILE*


#ifndef COUNT_OF
#  define COUNT_OF(a) (sizeof(a) / sizeof(*(a)))
#endif


typedef struct {
  const char *start;
  const char *end;   // One past end of range
} StringRange;

// Variant used for functions that modify the underlying string
// and may change the start pointer.
typedef struct {
  char *start;
  char *end;   // One past end of range
} AppendRange;


// For direct initialization from an array:
//   char a[100];
//   StringRange foo = RANGE_FROM_ARRAY(a);
#define RANGE_FROM_ARRAY(s) {.start = (char *)(s), .end = (char *)(s)+COUNT_OF(s)}

// In a printf() format string use the "%.*s" specifier and use this macro to generate the two arguments
#define RANGE_FMT(rp) (int)range_size(rp), (rp)->start


// ******************** Range initialization ********************

#define range_init(r, s, l) do { \
    (r)->start = (s);       \
    (r)->end   = (s) + (l); \
  } while(0)


// Span of the full range ignoring NUL chars
#define range_size(r) ((r)->end - (r)->start)

size_t range_strlen(StringRange *rng);


int range_copy_str(AppendRange *rng, const char *str, bool truncate);


// ******************** Append operations ********************
int range_cat_fmt(AppendRange *rng, const char *fmt, ...);

int range_cat_str(AppendRange *rng, const char *str);
int range_cat_str_no_nul(AppendRange *rng, const char *str);

int range_cat_range(AppendRange *rng, StringRange *src_rng);
int range_cat_range_no_nul(AppendRange *rng, StringRange *src_rng);

int range_cat_char(AppendRange *rng, char ch);
int range_cat_char_no_nul(AppendRange *rng, char ch);


// ******************** Whitespace trimming ********************
void range_ltrim(StringRange *rng);
void range_rtrim(StringRange *rng);
void range_trim(StringRange *rng);
void range_terminate(AppendRange *rng);

// ******************** Range output ********************
void range_puts(StringRange *rng);
void range_fputs(StringRange *rng, FILE *stream);

// ******************** Comparison ********************

bool range_eq(StringRange *rng, const char *str);
bool range_eq_range(StringRange *rng, StringRange *rng2);
bool range_is_int(StringRange *rng);

// ******************** Tokenizing ********************
bool range_token(const char *str, const char *delim, StringRange *token);
bool range_token_limit(const char *str, const char *delim, StringRange *token, size_t *limit);

#endif // RANGE_STRINGS_H
