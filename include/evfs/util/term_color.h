/* SPDX-License-Identifier: MIT
Copyright 2022 Kevin Thibedeau
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

#ifndef TERM_COLOR_H
#define TERM_COLOR_H


// ******************** Preconstucted ANSI color macros ********************
#define A_BLK "\033[0;30m"
#define A_RED "\033[0;31m"
#define A_GRN "\033[0;32m"
#define A_YLW "\033[0;33m"
#define A_BLU "\033[0;34m"
#define A_MAG "\033[0;35m"
#define A_CYN "\033[0;36m"
#define A_WHT "\033[0;37m"


#define A_BOLD "\033[1m"

#define A_BBLK "\033[1;30m"
#define A_BRED "\033[1;31m"
#define A_BGRN "\033[1;32m"
#define A_BYLW "\033[1;33m"
#define A_BBLU "\033[1;34m"
#define A_BMAG "\033[1;35m"
#define A_BCYN "\033[1;36m"
#define A_BWHT "\033[1;37m"

#define A_NONE "\033[0m"


// Color codes. Use these to track the current active color selection.
enum ANSIColor {
  ANSI_NONE = 0,
  ANSI_BLK = 30,
  ANSI_RED = 31,
  ANSI_GRN = 32,
  ANSI_YLW = 33,
  ANSI_BLU = 34,
  ANSI_MAG = 35,
  ANSI_CYN = 36,
  ANSI_WHT = 37
};

#if 0
void term_color(enum ANSIColor c) {
  if(c != ANSI_NONE)
    printf("\033[0;%dm", c);
  else
    fputs(A_NONE, stdout);
}

void term_color_bold(enum ANSIColor c) {
  if(c != ANSI_NONE)
    printf("\033[1;%dm", c);
  else
    fputs(A_NONE, stdout);
}

void term_color_bg(enum ANSIColor c) {
  if(c != ANSI_NONE)
    printf("\033[%dm", c+10);
  else
    fputs(A_NONE, stdout);
}
#endif

#endif // TERM_COLOR_H
