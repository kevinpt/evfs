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
getopt_r

This is a reentrant implementation of getopt(). It operates similarly to
standard getopt() with the addition of a state struct to avoid global state.
The argc argument is not used in this version.
------------------------------------------------------------------------------
*/

#ifndef GETOPT_R_H
#define GETOPT_R_H

typedef struct {
  const char *next_char; // For internal tracking by getopt_r()

  const char *optarg;    // Argument to current option
  int optind;            // Index for next option to process
  int optopt;            // Current option letter or error codes ('?' or ':')

  bool report_errors;    // Set true for error messages
} GetoptState;


int getopt_r(char * const argv[], const char *optstring, GetoptState *state);


#define getopt_init(s)   memset(s, 0, sizeof(*(s)))

#endif // GETOPT_R_H
