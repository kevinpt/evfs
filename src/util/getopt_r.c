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

The state struct should be initialized to 0 on creation or by using the
getopt_init() macro.

The GNU extension for optional arguments is supported. These use a double ':'
in the optstring. Long arguments are not supported.

Error reporting is enabled by setting report_errors true in the struct.

The struct fields optarg, optind, and optopt correspond to the same globals
used by standard getopt().
------------------------------------------------------------------------------
*/

#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include "getopt_r.h"


/*
Parse command line options

Args:
  argv:       Array of command arguments. Must be terminated by a NULL entry.
  optstring:  Option definition string
  state:      State from previous invocation of getopt_r()

Returns:
  New option character on success. -1 on end of parsing. '?' or ':' on parse failure
*/
int getopt_r(char * const argv[], const char *optstring, GetoptState *state) {

#define cur_opt argv[state->optind]

  if(!optstring || !state)
    return -1;

  // Initialize state if it looks dirty
  if(state->optind < 1) {
    state->optind = 1;
    state->next_char = NULL;
  }

  // Wipe out previous results
  state->optarg = NULL;
  state->optopt = '\0';

  if(!cur_opt || cur_opt[0] != '-' || !strcmp(cur_opt, "-")) {
    // Done processing options
    state->next_char = NULL;
    return -1;
  }

  if(!strcmp(cur_opt, "--")) {
    // Done processing options
    state->next_char = NULL;
    state->optind++; // Skip over "--" for extra arguments
    return -1;
  }


  if(!state->next_char || state->next_char[0] == '\0') { // We consumed previous arg; Start on next options
    state->next_char = &cur_opt[1]; // Skip over hyphen to set new start of option text
  }

  state->optopt = state->next_char[0]; // Report current option to caller if return value is discarded
  int rval = state->optopt;

  // Search optstring for the current option
  const char *opt_def = strchr(optstring, state->optopt);

  if(opt_def) {

    if(opt_def[1] == ':') { // This option takes an argument
      // Assume the argument follows immediately without space
      state->optarg = &state->next_char[1];

      if(state->optarg[0] == '\0') { // There is no arg included in this option

        if(opt_def[2] != ':') { // Mandatory argument
          state->optind++; // Look for argument in next position
          if(cur_opt) {
            state->optarg = cur_opt;
          } else { // Exhausted all options without finding an argument
            state->optarg = NULL;

            if(state->report_errors)
              fprintf(stderr, "ERROR: getopt_r() Missing required argument to option '-%c'\n", state->optopt);

            rval = optstring[0] == ':' ? ':' : '?';
          }

        } else { // Optional argument (GNU extension)
          // Check if the next arg doesn't begin with '-'
          state->optind++;
          if(cur_opt && cur_opt[0] != '-') { // We use this as the argument
            state->optarg = cur_opt;
          } else { // Revert and work without an argument
            state->optind--;
            state->optarg = NULL;
          }
        }
      }

      state->next_char = NULL; // Force switch to next arg
    }

  } else { // Option not found
    if(state->report_errors)
      fprintf(stderr, "ERROR: getopt_r() Unknown option '-%c'\n", state->optopt);

    rval = '?';
  }

  if(!state->next_char) { // All options in this arg are consumed
    state->optind++; // Work on next arg

  } else { // Prep for next option within this arg
    state->next_char++;
    if(state->next_char[0] == '\0') {
      state->optind++;
    }
  }

  return rval;
}

