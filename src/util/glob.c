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
String globbing

  General purpose glob pattern matcher
------------------------------------------------------------------------------
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>


/*
Test if a character is a member of a set

Args:
  ch:         Character to test
  match_set:  String of characters in the match set

Returns:
  true when ch is in the set
*/
bool char_match(char ch, const char *match_set) {
  for(const char *pos = match_set; *pos != '\0'; pos++) {
    if(*pos == ch) return true;
  }

  return false;
}


// A bit-set for each possible 8-bit character in a range definition
typedef struct {
  uint32_t chars[8]; // 256 bits
} CharSet;

#define ADD_TO_SET(cs, c)      ((cs)->chars[((uint8_t)(c)) / 32] |= (1 << ( ((uint8_t)(c)) % 32)) )
#define CHAR_IS_IN_SET(cs, c)  ((cs)->chars[((uint8_t)(c)) / 32] & (1 << ( ((uint8_t)(c)) % 32)) )
#define COUNT_OF(a)            (sizeof(a) / sizeof(*a))

static const char *parse_range_def(const char *pat_pos, CharSet *cs) {
  const char *rng_pos;
  char ch, end_ch;
  bool invert = false;

  if(*(pat_pos+1) == '!') {
    pat_pos++;
    invert = true;
  }

  // Scan until end of range
  for(rng_pos = pat_pos+1; *rng_pos != '\0' && *rng_pos != ']'; rng_pos++) {
    if(*(rng_pos+1) == '-') { // Span of chars: <b>-<e>
      end_ch = *(rng_pos+2);
      if(end_ch == ']' || end_ch == '\0') { // Syntax error: Missing end of span
        rng_pos = rng_pos+2;
        break;
      }

      for(ch = *rng_pos; ch <= end_ch; ch++) { // Add all chars in span
        ADD_TO_SET(cs, ch);
      }
      rng_pos += 2;
    } else { // Single character
      ADD_TO_SET(cs, *rng_pos);
    }
  }

  if(invert) {
    for(size_t i = 0; i < COUNT_OF(cs->chars); i++) {
      cs->chars[i] = ~cs->chars[i];
    }
  }

  return rng_pos;
}


/*
Perform a glob pattern match on a string

Similar to a simplified POSIX fnmatch()
Adapted from https://research.swtch.com/glob

Accepted pattern syntax:

  ?            Match a single character
  *            Match zero or more characters
  [abc]        Match any of a, b, or c
  [a-z]        Match range a-z
  [a-zABC0-9]  Match combined ranges and individual characters
  [!a-z]       Match inverted range


Args:
  pattern:         Glob pattern to match in string
  str:             String to perform match on
  dir_separators:  Characters to treat as directory separators

Returns:
  true when a match is found
*/
bool glob_match(const char *pattern, const char *str, const char *dir_separators) {
  const char *str_pos, *pat_pos;
  const char *str_star = NULL, *pat_star = NULL;
  char s_ch, p_ch;

  CharSet cs;

  if(!pattern || !str) return false;

  str_pos = str;
  pat_pos = pattern;

  while(*str_pos != '\0' || *pat_pos != '\0') {
    p_ch = *pat_pos;
    s_ch = *str_pos;

    if(p_ch != '\0') { // Test next char in pattern
      switch(p_ch) {
      case '*': // Variable length star match
        // Save star position so we can backtrack on mismatches
        pat_star = pat_pos;
        str_star = str_pos+1;
        pat_pos++;
        // Skip increment of str_pos so we can match 0 chars
        continue;
        break;

      case '?': // Wildcard match
        // Prevent wildcard match to directory separator.
        // Otherwise this is an automatic match.
        if(s_ch != '\0' && !char_match(s_ch, dir_separators)) {
          str_pos++;
          pat_pos++;
          continue;
        }
        break;

      case '[': // Character range
        memset(&cs, 0, sizeof(cs));
        pat_pos = parse_range_def(pat_pos, &cs);
        if(CHAR_IS_IN_SET(&cs, s_ch)) {
          str_pos++;
          pat_pos++;
          continue;
        }
        break;

      default: // Literal match
        if(s_ch == p_ch) {
          str_pos++;
          pat_pos++;
          continue;
        }
        break;
      }

    }

    // Mismatch; Recover with backtracking or fail
    if(pat_star && s_ch != '\0' && !char_match(s_ch, dir_separators)) {
      str_pos = str_star;
      pat_pos = pat_star;
      continue;
    }

    return false;
  }

  return true;
}



