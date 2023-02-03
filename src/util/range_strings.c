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


This library is built around a basic StringRange struct that points to a
substring in a separate array. The StringRange tracks the start and end
pointers of a substring which need not be NUL terminated. The end pointer
points to one char past the end of the range. The start pointer should
always be less than or equal to the end pointer. A StringRange is 0-length
when start and end are equal.

There are two general uses cases for this library. If you wish to refer to
substrings of existing strings you can represent them using a StringRange
object and avoid copying to another buffer. You can also build strings using
the AppendRange struct which only differs by having non-const pointers. In
this case the range refers to available empty space in the parent string.
Append operations advance the start pointer so that they can be performed
repeatedly in a sequence.

  StringRange:
    Represents a buffer span wholly or partially occupied by a string.

                   start                        end
                    \/                          \/
               rng: [f][o][o][b][a][r][\0][ ][ ]( )

                    range_size(&rng);   --> 9
                    range_strlen(&rng); --> 6

    It can be a substring within a larger buffer. NUL termination is
    not required.

                         start  end
                          \/    \/
               rng: [f][o][o][b][a][r][\0][ ][ ][ ]

                    Range represents substring "ob"


  AppendRange:
    Represents an unused buffer span. Append operations will advance
    the start pointer until it reaches the end.
                                     start      end
                                      \/        \/
               rng: [f][o][o][b][a][r][\0][ ][ ]( )


               range_cat_str(&rng, "bz");

                                         start  end
                                            \/  \/
               rng: [f][o][o][b][a][r][b][z][\0]( )


In any case, a StringRange needs to point into valid memory before it can be
used. General initialization for arrays and pointers is accomplished with the
range_init() macro:

  char buf[100];
  StringRange buf_r;

  range_init(&buf_r, buf, sizeof(buf));


  char *str;
  StringRange str_r;
  ...
  range_init(&str_r, str, strlen(str)+1); // You should include existing NUL in new ranges

An alternative initializer macro can be used for arrays:

  char buf[100];
  StringRange buf_r = RANGE_FROM_ARRAY(buf);

This utility library does not do any memory management. You are responsible for
allocation and release of the memory that backs the StringRange objects. The
objective is to be a lightweight adjunct to the standard string library.

StringRange objects consist of only two pointers and can be passed by value if
desired. They can operate in place of the usual pair of arguments used to pass
buffer pointers and their length into functions.

The end pointer is best treated as opaque but you can always safely access the
start pointer to pass the start of the range to code expecting a pointer:

  void string_func(const char *str);

  StringRange str_r;
  ...
  range_cat_str(&str_r, "foobar");
  string_func(str_r.start);

The normal append operations will always terminate the string with NUL so that
it's always a valid C string. There are also a set of equivalent operations
that don't append NUL for special cases where you don't want to overwrite
existing data. Functions returning substrings won't add NULs so you should be
prepared to deal with non-C strings. You can use the range_strlen() function
to check the length of a StringRange's data with or without a NUL. This is
different from the range_size() macro which always returns the full span of
the range regardless of any NUL.

When printing StringRange objects in printf() style formatted output you can
use the "%.*s" specifier along with the RANGE_FMT() macro to output sub-strings
without guaranteed NULs:

  StringRange *r;
  printf("String is: '%.*s'\n", RANGE_FMT(r));

You can also use the PRISR format macro:

  printf("String is: '%" PRISR "'\n", RANGE_FMT(r));

Tokenizer functions range_token() and range_token_limit() serve as substitutes
for strtok_r() with the benefit that the input string is not altered.
------------------------------------------------------------------------------
*/

#include <string.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdbool.h>
#include <ctype.h>
#include <assert.h> // For static_assert
#include "util/range_strings.h"
#include "util/intmath.h"


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
    size_t len = range_size(rng);
    if(len <= 1)  // Force single byte range to zero for formatted length query
      len = 0;
    rval = vsnprintf(rng->start, len, fmt, args);
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


int range_cat_vfmt(AppendRange *rng, const char *fmt, va_list args) {
  int rval = 0;

  if(rng->start < rng->end) { // Range is valid?
    size_t len = range_size(rng);
    if(len <= 1)  // Force single byte range to zero for formatted length query
      len = 0;
    rval = vsnprintf(rng->start, len, fmt, args);

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


/*
Concatenate an unsigned integer to a range

This performs integer formatting on its own and is safe to use where printf()
family functions would consume more stack than is available.

Args:
  rng:  Target string for value
  n:    Positive integer to format into rng

Returns:
  Number of bytes written if positive
  Number of bytes needed if negative
*/
int range_cat_uint(AppendRange *rng, uint32_t n) {
  if(n == 0)
    return range_cat_char(rng, '0');

  char buf[10+1]; // log10(2^32) --> 10 digits max
  char *pos = &buf[9];
  buf[10] = '\0';

  while(n) {
    char digit = (char)(n % 10) + '0';
    n /= 10;
    *pos-- = digit;
  }
  pos++;

  return range_cat_str(rng, pos);
}


/*
Concatenate an integer to a range

This performs integer formatting on its own and is safe to use where printf()
family functions would consume more stack than is available.

Args:
  rng:  Target string for value
  n:    Integer to format into rng

Returns:
  Number of bytes written if positive
  Number of bytes needed if negative
*/
int range_cat_int(AppendRange *rng, int32_t n) {
  int s_chars = 0;
  if(n < 0) {
    n = -n; // NOTE: Does not work with INT_MIN
    s_chars = range_cat_char(rng, '-');
  }

  int n_chars = range_cat_uint(rng, n);
  if(n_chars < 0 && s_chars > 0)
    s_chars = -s_chars;

  return s_chars + n_chars;
}


/*
Concatenate an unsigned fixed point integer to a range

Args:
  rng:        Target string for value
  value:      Fixed point value to format
  scale:      Scale factor for value
  places:     Number of fractional decimal places in output
  pad_digits: Pad with spaces: 0 = no padding, < 0 = left justify, > 0 = right justify

Returns:
  Number of bytes written if positive
  Number of bytes needed if negative
*/
int range_cat_ufixed_padded(AppendRange *rng, unsigned value, unsigned scale, unsigned places,
                            signed pad_digits) {
  unsigned integer = value / scale;
  unsigned frac = value % scale;
  int status = 0;
  int len;

  // Give ourselves an extra digit for initial rounding
  unsigned scale_b10_digits = base10_digits(scale) + 1;

  // get 10 ^ (base10_digits(scale)+1)
  unsigned scale_b10 = 1;
  for(unsigned i = 0; i < scale_b10_digits-1; i++) {
    scale_b10 *= 10;
  }

  // Convert fraction to base-10
  frac = frac * scale_b10 / scale;

  // Round up and remove extra digit
  frac = (frac + 5) / 10;
  scale_b10 /= 10;  // Set to true base-10 scale

  unsigned frac_digits = scale_b10_digits - 2;  // Remove extra digit for round

  if(places > 0) {
    // Reduce digits if fraction is larger than requested decimal places
    while(frac_digits > places+1) { // Initial scale down without rounding
      frac /= 10;
      frac_digits--;
    }

    if(frac_digits > places) { // Final adjust with rounding
      frac += 5;  // Start round up in last decimal place

      if(frac > scale_b10) { // Rounded into integer
        integer++;
        frac = 0; // No fraction part
        frac_digits = 1;

      } else {  // Finish last adjustment
        frac /= 10;
        frac_digits--;
      }
    }

    status = 0;
    if(pad_digits > 0) { // Handle right justification here
      // Get length of formatted string
      len = snprintf(NULL, 0, "%u.%0*u", integer, frac_digits, frac);

      if(pad_digits > len) {
        pad_digits -= len;

        for(int i = 0; i < pad_digits; i++) {
          if(range_cat_char(rng, ' ') < 0) {
            status = -pad_digits;
            break;
          }
          status++;
        }
      }
    }

    if(status >= 0) { // Padding was ok
      len = range_cat_fmt(rng, "%u.%0*u", integer, frac_digits, frac);
      if(len >= 0)
        status += len;
      else // Won't fit
        status = -status + len;
    }

  } else {  // places == 0; No fraction part
    // Check for rounding into integer
    if(frac + scale_b10/2 >= scale_b10)  // Round up in 1/10ths place
      integer++;

    // Handle right justification here
    int justify = (pad_digits < 0) ? 0 : pad_digits;
    status = range_cat_fmt(rng, "%*u", justify, integer);
  }

  if(pad_digits < 0) {
    if(status >= 0) {  // Left justify by padding on right
      len = 0;
      for(int i = pad_digits; i < 0; i++) {
        if(range_cat_char(rng, ' ') < 0) {
          len = pad_digits;
          break;
        }

        len++;
      }

      if(len >= 0)
        status += len;
      else  // Won't fit
        status = -status + len;

    } else { // Number didn't fit
      if(pad_digits < status) // Both are negative
        status = pad_digits;
    }
  }

  return status;
}


/*
Concatenate an unsigned fixed point integer to a range

Args:
  rng:          Target string for value
  value:        Fixed-point value to format
  fp_scale:     Fixed-point scale factor for value
  frac_places:  Number of fractional decimal places in output, -1 for max precision
  pad_digits:   Pad with spaces: 0 = no padding, < 0 = left justify, > 0 = right justify

Returns:
  Number of bytes written if positive
  Number of bytes needed if negative
*/
int range_cat_fixed_padded(AppendRange *rng, long value, unsigned fp_scale, int frac_places,
                            signed pad_digits) {

  long integer;
  long frac;

  int frac_digits = to_fixed_base10_parts(value, fp_scale, &integer, &frac);
  if(frac_places >= 0)  // Rescale
    frac_digits = fixed_base10_adjust(&integer, &frac, frac_digits, frac_places);

  if(frac < 0) frac = -frac;

  // Convert exponent to scaling
  int scale_b10 = 1;
  for(int i = 0; i < frac_digits; i++) {
    scale_b10 *= 10;
  }

  int status = 0;
  int len;

  // Apply justification padding
  if(frac_digits > 0) {
    status = 0;
    if(pad_digits > 0) { // Handle right justification here (Padding on left side)
      // Get length of formatted string
      len = snprintf(NULL, 0, "%ld.%0*ld", integer, frac_digits, frac);

      if(pad_digits > len) {
        pad_digits -= len;

        for(int i = 0; i < pad_digits; i++) { // Add padding
          if(range_cat_char(rng, ' ') < 0) {
            status = -pad_digits;
            break;
          }
          status++;
        }
      }
    }

    if(status >= 0) { // Left padding was ok
      len = range_cat_fmt(rng, "%ld.%0*ld", integer, frac_digits, frac);
      if(len >= 0)
        status += len;
      else // Won't fit
        status = -status + len;
    }

  } else {  // No fraction part
    // Handle right justification here (Padding on left side)
    int justify = (pad_digits < 0) ? 0 : pad_digits;
    status = range_cat_fmt(rng, "%*ld", justify, integer);
  }

  if(pad_digits < 0) {
    pad_digits = -pad_digits;
    if(status >= 0 && pad_digits > status) {  // Left justify by padding on right
      len = 0;
      for(int i = status - pad_digits; i < 0; i++) {
        if(range_cat_char(rng, ' ') < 0) {
          len = status - pad_digits;
          break;
        }

        len++;
      }

      if(len >= 0)
        status += len;
      else  // Won't fit
        status = -status + len;

    } else { // Number didn't fit
      if(pad_digits < status) // Both are negative
        status = pad_digits;
    }
  }

  return status;
}


/*
Pad out a string to fill a range

Args:
  rng:  String to pad
  pad:  Padding character

Returns:
  Number of pad bytes written
*/
int range_pad_right(StringRange *rng, char pad) {
  size_t width = range_size(rng) - 1;

  size_t pad_num = 0;
  size_t rng_len = range_strlen(rng);
  char *pad_pos = (char *)&rng->start[rng_len];
  if(rng_len < width) {
    pad_num = width - rng_len;
    for(size_t i = 0; i < pad_num; i++) {
      *pad_pos++ = pad;
    }
    *pad_pos = '\0';
  }

  return pad_num;
}


// ******************** Whitespace trimming ********************

/*
Trim whitespace from the left end of a range

Args:
  rng: The range to trim
*/
void range_ltrim(StringRange *rng) {
  const char *pos = rng->start;

  while(pos < rng->end && isspace((unsigned char)*pos))
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
    if(!isspace((unsigned char)*pos) && *pos != '\0')
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


/*
Truncate range to a shorter length

Args:
  rng: The range to truncate
  len: New length of the string in rng
*/
void range_set_len(StringRange *rng, size_t len) {
  size_t maxlen = range_size(rng);
  if(len >= maxlen)
    return;

  memset((char *)&rng->start[len], 0, maxlen - len);
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
    if(!isdigit((unsigned char)*rpos)) return false;
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

