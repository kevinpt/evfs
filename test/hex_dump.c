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
Routines for dumping the contents of a data buffer.

------------------------------------------------------------------------------
*/

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <ctype.h>
#include "hex_dump.h"


// ANSI color macros
#define A_BLK "\033[0;30m"
#define A_RED "\033[0;31m"
#define A_GRN "\033[0;32m"
#define A_YLW "\033[0;33m"
#define A_BLU "\033[0;34m"
#define A_MAG "\033[0;35m"
#define A_CYN "\033[0;36m"
#define A_WHT "\033[0;37m"

#define A_BBLK "\033[1;30m"
#define A_BRED "\033[1;31m"
#define A_BGRN "\033[1;32m"
#define A_BYLW "\033[1;33m"
#define A_BBLU "\033[1;34m"
#define A_BMAG "\033[1;35m"
#define A_BCYN "\033[1;36m"
#define A_BWHT "\033[1;37m"

#define A_NONE "\033[0m"


// Dump a line of hex data
static void hex_dump(size_t line_addr, size_t buf_addr, uint8_t *buf, size_t buf_len,
                     int indent, int addr_size, bool show_ascii, bool ansi_color) {
  size_t offset = buf_addr - line_addr;
  unsigned int line_bytes = 16;

  // Indent line and show address
  if(ansi_color)
    fputs(A_BLU, stdout);

  printf("%*s%0*lX  ", indent, " ", addr_size, line_addr);

  if(ansi_color)
    fputs(A_NONE, stdout);


  // Print leading gap
  for(size_t i = 0; i < offset; i++) {
    fputs("   ", stdout);
  }
  // Print data
  bool color_on = false;
  for(size_t i = 0; i < buf_len; i++) {
    if(isgraph(buf[i])) {
      if(ansi_color && !color_on)
        fputs(A_YLW, stdout);
      color_on = true;
      printf("%02X ", buf[i]);
    } else {
      if(ansi_color && color_on)
        fputs(A_NONE, stdout);
      color_on = false;
      printf("%02X ", buf[i]);
    }
  }
  if(ansi_color && color_on)
    fputs(A_NONE, stdout);


  // Print trailing gap
  if(buf_len + offset < line_bytes) {
    offset = line_bytes - (buf_len + offset);

    for(size_t i = 0; i < offset; i++) {
      fputs("   ", stdout);
    }
  }

  if(show_ascii) {
    if(ansi_color)
      fputs(A_GRN " |" A_NONE, stdout);
    else
      fputs(" |", stdout);

    offset = buf_addr - line_addr;
    // Print leading gap
    for(size_t i = 0; i < offset; i++) {
      fputs(" ", stdout);
    }
    // Print data
    color_on = false;
    for(size_t i = 0; i < buf_len; i++) {
      if(isgraph(buf[i])) {
        if(ansi_color && !color_on)
          fputs(A_YLW, stdout);
        color_on = true;

        printf("%c", buf[i]);
      } else {
        if(ansi_color && color_on)
          fputs(A_NONE, stdout);
        color_on = false;

        printf(".");
      }
    }
    if(ansi_color && color_on)
      fputs(A_NONE, stdout);

    // Print trailing gap
    if(buf_len + offset < line_bytes) {
      offset = line_bytes - (buf_len + offset);

      for(size_t i = 0; i < offset; i++) {
        fputs(" ", stdout);
      }
    }

    if(ansi_color)
      fputs(A_GRN "|" A_NONE, stdout);
    else
      fputs("|", stdout);

  }
  
  printf("\n");
}



static inline bool all_zeros(uint8_t *buf, size_t buf_size) {
  uint8_t *buf_end = buf + buf_size;

  while(buf < buf_end) {
    if(*buf++ != 0) return false;
  }

  return true;
}


#define DEFAULT_INDENT  4
#define ADDR_LEN        4

/*
Dump the contents of a buffer to stdout in hex format

This dumps lines of data with the offset, hex values, and printable ASCII

Args:
  buf        : Buffer to dump
  buf_len    : Length of buf data
  skip_zeros : Skip lines with all zeros
  show_ascii : Show table of printable ASCII on right side
  ansi_color : Print dump with color output
*/
void dump_array_ex(uint8_t *buf, size_t buf_len, bool skip_zeros, bool show_ascii, bool ansi_color) {
  size_t buf_pos, buf_count;
  size_t line_addr, line_offset;
  unsigned int line_bytes = 16;
  bool prev_skipped = false;

  line_addr = 0;
  line_offset = 0;

  buf_pos = 0;

  if(buf_len > 0) { // Chunk with data
    while(buf_pos < buf_len) {
      buf_count = (buf_len - buf_pos) < line_bytes ? buf_len - buf_pos : line_bytes;
      buf_count -= line_offset;

      bool cur_zeros = buf_count >= line_bytes && all_zeros(&buf[buf_pos], buf_count);
      bool skip_line = skip_zeros && cur_zeros;

      if(!skip_line || !prev_skipped) {
        hex_dump(line_addr, line_addr + line_offset, &buf[buf_pos], buf_count,
                DEFAULT_INDENT, ADDR_LEN, show_ascii, ansi_color);
      }

      if(skip_line && !prev_skipped) { // Show ellipsis 
          printf("%*s...\n", DEFAULT_INDENT, " ");
      }

      prev_skipped = skip_line;


      buf_pos += buf_count;
      if(buf_count + line_offset == line_bytes) // Increment address after full line
        line_addr += line_bytes;
      line_offset = 0;
    }

    line_offset = buf_count; // Next chunk may need to offset first line
  }

}


/*
Dump the contents of a buffer to stdout in hex format

This dumps lines of data with the offset, hex values, and printable ASCII

Args:
  buf     : Buffer to dump
  buf_len : Length of buf data
*/
void dump_array(uint8_t *buf, size_t buf_len) {
  dump_array_ex(buf, buf_len, /*skip_zeros*/ true, /*show_ascii*/ true, /*ansi_color*/ true);
}

