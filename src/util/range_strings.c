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

/*
------------------------------------------------------------------------------
Utility functions for operating on string ranges


This library is built around a basic RangeString struct that points to a
substring in a separate array. The RangeString tracks the start and end
pointers of a substring which need not be NUL terminated. The end pointer
points to one char past the end of the range. The start pointer should
always be less than or equal to the end pointer. A StringRange is 0-length
then start and end are equal.

There are two general uses cases for this library. If you wish to refer to
substrings of existing strings you can represent them using a StringRange
object and avoid copying to another buffer. You can also build strings using
the AppendRange struct which only differs by having non-const pointers. In
this case the range refers to available empty space in the parent string.
Append operations advance the start pointer so that they can be performed
repeatedly in a sequence.

In any case, a RangeString needs to point into valid memory before it can be
used. General initialization for arrays and pointers is accomplished with the
range_init() macro:

  char buf[100];
  StringRange buf_r;

  range_init(&buf_r, buf, sizeof(buf));


  char *str;
  StringRange str_r;

  range_init(&str_r, str, strlen(str)+1); // You should include existing NUL in new ranges

An alternative initializer macro can be used for arrays:

  char buf[100];
  StringRange buf_r = RANGE_FROM_ARRAY(buf);

This utility library does not do any memory management. You are responsible for
any allocation and release of the memory that backs the StringRange objects.
The objective is to be a lightweight adjunct to the standard string library.

RangeString objects consist of only two pointers and can be passed by value if
desired. They can operate in place of the usual pair of arguments used to pass
buffer pointers and their length into functions.

The end pointer is best treated as opaque but you can always safely access the
start pointer to pass the start of the range to code expecting a pointer.

The normal append operations will always terminate the string with NUL so that
is always a valid C string. There are aso a set of equivalent operations that
don't append NUL for special cases where you don't want to overwrite existing
data. Functions returning substrings won't add NULs so you should be prepared
to deal with non-C strings. You can use the range_strlen() function to check
the length of a StringRange's data with or without a NUL. This is different
from the range_size() macro which always returns the full span of the range
regardless of any NUL.

When printing RangeString objects in printf() style formatted output you can
use the "%.*s" specifier along with the RANGE_FMT() macro to output sub-strings
without guaranteed NULs:

  StringRange *r;
  printf("String is: '%.*s'\n", RANGE_FMT(r));


Tokenizer functions range_token() and range_token_limit() serve as substitutes
for strtok_r() with the benefit that the input string is not altered.
------------------------------------------------------------------------------
*/

#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <stdbool.h>
#include <ctype.h>
#include "range_strings.h"


// ******************** Range initialization ********************

/*
Get the length of a string in a range

Args:
  rng:      String to get length from

Returns:
  Length of the string in rng.
  If the string is NUL terminated the result is the same as strlen().
  If there is no NUL the result is the same as range_size().
*/
size_t range_strlen(StringRange *rng) {
  const char *pos = rng->start;

  while(pos < rng->end) {
    if(*pos == '\0') break;
    pos++;
  }

  return pos - rng->start;
}


/*
Copy a string into a range

Args:
  rng:      Destination range
  str:      Source string
  truncate: Truncates str when it is longer than rng

Returns:
  Number of bytes copied to range. On truncation, if `truncate` is true the value is the negative byte count
  less one (to distinguish 0). If `truncate` is false, `rng` is a 0-length string and the negative length
  of `str` is returned.
*/
int range_copy_str(AppendRange *rng, const char *str, bool truncate) {
  int rval;
  const char *src = str;
  char *dest = rng->start;
  bool done = false;

  while(dest < rng->end) {
    *dest++ = *src;
    if(*src == '\0') {
      done = true;
      break;
    }
    src++;
  }

  rval = src - str; // Number of bytes copied

  if(!done) { // Truncated
    if(truncate) {
      dest--;
      *dest = '\0';
      rval = -(rval-1);
    } else {
      if(rng->start)
        *rng->start = '\0'; // Wipe partial string
      rval = -(rval + strlen(src)); // Add uncopied bytes
    }
  }

  return rval;
}


// ******************** Append operations ********************

/*
Append a formatted string into a string range.

The range is modified to reflect the free space remaining.

Args:
  rng:      Target string for formatted arguments
  fmt:      Format string
  var_args: Additional arguments for the `fmt` string

Returns:
  Number of bytes written if positive
  Number of bytes needed if negative
*/
int range_cat_fmt(AppendRange *rng, const char *fmt, ...) {
  va_list args;
  int rval = 0;

  if(rng->start < rng->end) { // Range is valid?
    va_start(args, fmt);
    rval = vsnprintf(rng->start, range_size(rng), fmt, args);
    va_end(args);

    if(rval > 0) {
      if(rval < range_size(rng)) { // Success
        rng->start += rval;
      } else { // Truncated
        if(rng->start)
          *rng->start = '\0'; // Wipe partial string
        rval = -rval;
      }
    }

  }

  return rval;
}


/*
Concatenate a string to a range

Args:
  rng: Range to append `str` onto
  str: String to append

Returns:
  Number of bytes copied on success. On failure `rng` is a 0-length and the negative length
  of `str` is returned.
*/
int range_cat_str(AppendRange *rng, const char *str) {
  int rval;
  const char *src = str;
  char *dest = rng->start;
  bool done = false;

  while(dest < rng->end) {
    *dest++ = *src;
    if(*src == '\0') {
      done = true;
      break;
    }
    src++;
  }

  rval = src - str; // Number of bytes copied

  if(done) {
    rng->start = dest-1;
  } else { // Truncated
    if(rng->start)
      *rng->start = '\0'; // Wipe partial string
    rval = -(rval + strlen(src)); // Add uncopied bytes
  }

  return rval;
}


/*
Concatenate a string to a range without adding a NUL

Args:
  rng: Range to append `str` onto
  str: String to append

Returns:
  Number of bytes copied on success. On failure `rng` is a 0-length and the negative length
  of `str` is returned.
*/
int range_cat_str_no_nul(AppendRange *rng, const char *str) {
  int rval;
  const char *src = str;
  char *dest = rng->start;
  bool done = false;

  while(dest < rng->end) {
    if(*src == '\0') {
      done = true;
      break;
    }
    *dest++ = *src;
    src++;
  }

  rval = src - str; // Number of bytes copied

  if(done) {
    rng->start = dest-1;
  } else { // Truncated
    if(rng->start)
      *rng->start = '\0'; // Wipe partial string
    rval = -(rval + strlen(src)); // Add uncopied bytes
  }

  return rval;
}


/*
Concatenate a range to a range

Args:
  rng:     Range to append `src_rng` onto
  src_rng: Range to append

Returns:
  Number of bytes copied on success. On failure `rng` is a 0-length and the negative length
  of `src_rng` is returned.
*/
int range_cat_range(AppendRange *rng, StringRange *src_rng) {
  int rval;
  const char *src = src_rng->start;
  char *dest = rng->start;
  bool done = false;


  while(dest < rng->end && src < src_rng->end) {
    *dest++ = *src;
    if(*src == '\0') {
      done = true;
      break;
    }
    src++;
  }

  rval = src - src_rng->start; // Number of bytes copied


  // Add NUL if src_rng is a substring
  if(!done && dest < rng->end) {
    *dest++ = '\0';
    done = true;
  }


  if(done) {
    rng->start = dest-1;
  } else { // Truncated
    if(rng->start)
      *rng->start = '\0'; // Wipe partial string
    rval = -(rval + strnlen(src, src_rng->end - src)); // Add uncopied bytes
  }

  return rval;
}


/*
Concatenate a range to a range without adding a NUL

Args:
  rng:     Range to append `src_rng` onto
  src_rng: Range to append

Returns:
  Number of bytes copied on success. On failure `rng` is a 0-length and the negative length
  of `src_rng` is returned.
*/
int range_cat_range_no_nul(AppendRange *rng, StringRange *src_rng) {
  int rval;
  const char *src = src_rng->start;
  char *dest = rng->start;
  bool done = false;


  while(dest < rng->end && src < src_rng->end) {
    if(*src == '\0') {
      done = true;
      break;
    }
    *dest++ = *src;
    src++;
  }

  rval = src - src_rng->start; // Number of bytes copied


  // Add NUL if src_rng is a substring
  if(!done && dest < rng->end) {
    //*dest++ = '\0';
    done = true;
  }


  if(done) {
    rng->start = dest;
  } else { // Truncated
    if(rng->start)
      *rng->start = '\0'; // Wipe partial string
    rval = -(rval + strnlen(src, src_rng->end - src)); // Add uncopied bytes
  }

  return rval;
}


/*
Concatenate a character to a range

Args:
  rng: Range to append `ch` onto
  ch:  Character to append

Returns:
  1 on success, -1 on failure.
*/
int range_cat_char(AppendRange *rng, char ch) {
  if(rng->start && rng->start < rng->end-1) {
    *rng->start++ = ch;
    *rng->start = '\0';
    return 1;
  }

  return -1;
}

/*
Concatenate a character to a range without adding a NUL

Args:
  rng: Range to append `ch` onto
  ch:  Character to append

Returns:
  1 on success, -1 on failure.
*/
int range_cat_char_no_nul(AppendRange *rng, char ch) {
  if(rng->start && rng->start < rng->end-1) {
    *rng->start++ = ch;
    return 1;
  }

  return -1;
}



// ******************** Whitespace trimming ********************

/*
Trim whitespace from the left end of a range

Args:
  rng: The range to trim
*/
void range_ltrim(StringRange *rng) {
  const char *pos = rng->start;

  while(pos < rng->end && isspace(*pos))
    pos++;

  rng->start = pos;
}

/*
Trim whitespace from the right end of a range

Args:
  rng: The range to trim
*/
void range_rtrim(StringRange *rng) {
  const char *pos = rng->start + strnlen(rng->start, rng->end - rng->start)-1;

  while(pos >= rng->start) {
    if(!isspace(*pos) && *pos != '\0')
       break;

    pos--;
  }

  rng->end = pos+1;;
}

/*
Trim whitespace from both ends of a range

Args:
  rng: The range to trim
*/
void range_trim(StringRange *rng) {
  range_ltrim(rng);
  range_rtrim(rng);
}

/*
Add a NUL terminator to a range.

If rng lacks a NUL one is added before the end pointer.

Args:
  rng: The range to terminate
*/
void range_terminate(AppendRange *rng) {
  size_t maxlen = range_size(rng);
  size_t rlen = strnlen(rng->start, maxlen);

  if(rlen == maxlen) { // No NUL found
    *(rng->end-1) = '\0';
  }
}


// ******************** Range output ********************

/*
Print a range to stdout.

Args:
  rng: The range to print
*/
void range_puts(StringRange *rng) {
  const char *pos = rng->start;

  while(pos < rng->end) {
    if(*pos == '\0')
      break;

    putc(*pos++, stdout);
  }
}

/*
Print a range to an I/O stream.

Args:
  rng: The range to print
*/
void range_fputs(StringRange *rng, FILE *stream) {
  const char *pos = rng->start;

  while(pos < rng->end) {
    if(*pos == '\0')
      break;

    putc(*pos++, stream);
  }
}


// ******************** Comparison ********************

bool range_eq(StringRange *rng, const char *str) {
  const char *rpos = rng->start;
  const char *spos = str;

  while(rpos < rng->end && *spos != '\0') {
    if(*rpos != *spos) return false;
    rpos++;
    spos++;
  }

  return (rpos == rng->end && *spos == '\0');
}


bool range_eq_range(StringRange *rng, StringRange *rng2) {
  const char *rpos = rng->start;
  const char *r2pos = rng2->start;

  while(rpos < rng->end && r2pos < rng2->end) {
    if(*rpos != *r2pos) return false;
    rpos++;
    r2pos++;
  }

  return (rpos == rng->end && r2pos == rng2->end);
}


bool range_is_int(StringRange *rng) {
  const char *rpos = rng->start;

  while(rpos < rng->end) {
    if(!isdigit(*rpos)) return false;
    rpos++;
  }

  return true;
}

// ******************** Tokenizing ********************

/*
Extract tokens from a string.

Works similar to strtok_r() but does not destructively modify str.
Pass `str` on first invocation then use NULL on subsequent calls to get all tokens.

Args:
  str:   C string to tokenize. Only passed on first invocation. NULL afterwards.
  delim: Delimiters for tokens
  token: Output range representing the token

Returns:
  true when a valid substring is in the token range. false when done parsing.
*/
bool range_token(const char *str, const char *delim, StringRange *token) {
  if(!token || !delim)
    return false;

  if(!str) // Use previous token for start point
    str = token->end;

  if(!str)
    return false;


  // Skip leading delimiters
  str += strspn(str, delim);
  if(*str == '\0') // No more tokens
    return false;

  // Get token length
  size_t tok_len = strcspn(str, delim);

  // Save token
  token->start = (char *)str;
  token->end = (char *)(str + tok_len);

  return true;
}


/*
Extract tokens from a string.

Works similar to strtok_r() but does not destructively modify str.
Pass `str` on first invocation then use NULL on subsequent calls to get all tokens.

This variant takes an in/out limit argument that allows parsing to end on strings
without a NUL termination. This allows parsing tokens from a substring without
having to copy it into a temp buffer.

Args:
  str:   C string to tokenize. Only passed on first invocation. NULL afterwards.
  delim: Delimiters for tokens
  token: Output range representing the token
  limit: Remaining characters to parse (use for non NUL-terminated strings)

Returns:
  true when a valid substring is in the token range. false when done parsing.
*/
bool range_token_limit(const char *str, const char *delim, StringRange *token, size_t *limit) {
  if(!token || !delim || !limit)
    return false;

  if(*limit == 0)
    return false;

  if(!str) // Use previous token for start point
    str = token->end;

  if(!str)
    return false;


  // Skip leading delimiters
  size_t consumed = strspn(str, delim);
  str += consumed;
  if(*str == '\0') // No more tokens
    return false;

  // Get token length
  size_t tok_len = strcspn(str, delim);
  if(consumed + tok_len > *limit) {// Truncate token
    if(consumed <= *limit)
      tok_len = *limit - consumed;
    else
      tok_len = 0;
  }
  consumed += tok_len;

  if(consumed > *limit)
    consumed = *limit;

  *limit -= consumed;

  // Save token
  token->start = (char *)str;
  token->end = (char *)(str + tok_len);

  return true;
}

