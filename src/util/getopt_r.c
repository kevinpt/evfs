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

/*
------------------------------------------------------------------------------
getopt_long_r

This is a reentrant implementation of getopt_long(). It operates similarly to
standard getopt_long() with the addition of a GetoptState struct to avoid global
state. The `argc` parameter is not used in this implementation. The argument list
*must* terminate with a NULL pointer. The long option index value is returned in
the state struct.

The state struct should be initialized to 0 on creation or by using the
:c:macro:`getopt_init` macro.

The struct fields `optarg`, `optind`, and `optopt` correspond to the same globals
used by standard getopt*().

Setting the `permute_args` member of the GetoptState struct to true enables shifting
non-option arguments to the end of the argument list. When option parsing
completes, the `optind` member points to the first non-option, if any.

Verbose error reporting is enabled by setting `report_errors` true in the struct.


When the short option string begins with a colon ':', a missing option argument
is reported by returning ':'. Otherwise, missing options are treated as a normal
error with a return of '?'.

The GNU extension for optional short arguments is supported. These use a double
colon in the optstring:

  getopt_long_r(argv, "a",   NULL, &state); // No argument       "-a"
  getopt_long_r(argv, "a:",  NULL, &state); // Required argument "-a [foo]" or "-a[foo]"
  getopt_long_r(argv, "a::", NULL, &state); // Optional argument "-a [foo]?"

------------------------------------------------------------------------------
*/

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#include "util/range_strings.h"
#include "util/getopt_r.h"
#include "util/term_color.h"


#define ERR_MISSING_OPT   ':'
#define ERR_UNKNOWN_OPT   '?'


// Shift an argv param to the right
static inline void permute_right(char *argv[], int move_ix, int shift_right) {
  char *permuted = argv[move_ix]; // Save permuted arg

  // Shift following args back
  for(int i = move_ix+1; i <= move_ix+shift_right; i++) {
    argv[i-1] = argv[i];
  }

  argv[move_ix + shift_right] = permuted;
}


// Permute non-options toward end of argv
static void permute_args(char *argv[], int non_opts, int non_opt_start, GetoptState *state) {
  int shift_right = non_opts;
  if(state->optarg && state->optarg == argv[state->optind-1])
    shift_right++;  // optarg is in separate parameter

  for(int i = 0; i < non_opts; i++) {
    permute_right((char **)argv, non_opt_start, shift_right);
  }

  // Adjust indices
  state->optind -= non_opts;
}



/*
Parse command line options

Args:
  argv:         Array of command arguments. Must be terminated by a NULL entry.
  optstring:    Short option definition string
  long_options: Optional list of long option definitions terminated by NULL object
  state:        State from previous invocation of getopt_long_r()

Returns:
  New option character on success. 0 for long option with non-NULL flag.
  -1 on end of parsing. '?' or ':' on parse failure
*/
int getopt_long_r(char * const argv[], const char *optstring, const struct option *long_options,
                  GetoptState *state) {

#define CUR_OPT argv[state->optind]

  int rval = -1;

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
  state->long_index = -1;


  // Scan for non-options
  int non_opts = 0;
  int non_opt_start = state->optind;
  if(state->permute_args) {
    while(CUR_OPT && (CUR_OPT[0] != '-' || CUR_OPT[1] == '\0')) {
      state->optind++;
      non_opts++;
    }
  }

  // Check for end of args, non-option, or bare "-"
  if(!CUR_OPT || CUR_OPT[0] != '-' || !strcmp(CUR_OPT, "-")) {
    // Done processing options
    state->next_char = NULL;
    if(non_opts > 0)  // Correct optind so it points to first non-option
      state->optind -= non_opts;
    return -1;
  }

  // Check for explicit option termination
  if(!strcmp(CUR_OPT, "--")) {
    // Done processing options
    state->optind++; // Skip over "--" for extra arguments

    if(non_opts > 0)
      permute_args((char **)argv, non_opts, non_opt_start, state);

    state->next_char = NULL;
    return -1;
  }


  if(CUR_OPT[1] == '-') {  // Parse long option
    state->next_char = NULL;

    if(!long_options)
      return -1;

    char *long_opt = &CUR_OPT[2];
    state->optind++;

    char *equal_pos = strchr(long_opt, '=');
    size_t long_opt_len = (equal_pos != NULL) ? (size_t)(equal_pos - long_opt) : strlen(long_opt);
    int long_match = -1;

    // Scan for matching option
    for(int i = 0; long_options[i].name; i++) {
      if(strncmp(long_opt, long_options[i].name, long_opt_len) != 0) // Prefix match
        continue;

      if(strlen(long_options[i].name) == long_opt_len) { // Check for exact match
        long_match = i;
        break;
      }
    }

    if(long_match >= 0) {
      switch(long_options[long_match].has_arg) {
      case no_argument:
        if(equal_pos != NULL) { // Invalid option with argument
          if(state->report_errors)
            fprintf(stderr, "ERROR: getopt_long_r() Option '--%.*s' takes no argument\n",
                    (int)long_opt_len, long_opt);

          // Indicate equivalent short option that caused error
          state->optopt = long_options[long_match].flag ? '\0' : long_options[long_match].val;
          rval = ERR_UNKNOWN_OPT;
        }
        break;

      case required_argument:
        if(equal_pos != NULL) { // Argument in this option
          state->optarg = equal_pos+1;
        } else {  // Argument in next option
          state->optarg = CUR_OPT;
          state->optind++;
        }

        if(!state->optarg) {  // Validate arg was found
          if(state->report_errors)
            fprintf(stderr, "ERROR: getopt_long_r() Missing required argument to option '--%s'\n",
                    long_opt);

          // Indicate equivalent short option that caused error
          state->optopt = long_options[long_match].flag ? '\0' : long_options[long_match].val;
          state->optind--;  // Revert to this option's arg position
          rval = ERR_UNKNOWN_OPT;
        }
        break;

      case optional_argument:
        // Optional arguments in long opts are only usable with '=' form
        if(equal_pos != NULL) { // Argument in this option
          state->optarg = equal_pos+1;
        }
        break;
      }

    } else {  // Unknown long option
      if(state->report_errors)
        fprintf(stderr, "ERROR: getopt_long_r() Unknown long option '--%s'\n",
                long_opt);
      state->optopt = '\0';
      rval = ERR_UNKNOWN_OPT;
    }

    if(rval == -1) {  // No errors
      state->long_index = long_match;

      if(long_options[long_match].flag) {
        *long_options[long_match].flag = long_options[long_match].val;
        rval = '\0';
      } else {
        rval = long_options[long_match].val;
      }

    }


  } else { // Parse short options

    if(!state->next_char || *state->next_char == '\0') { // We consumed previous arg; Start on next options
      state->next_char = &CUR_OPT[1]; // Skip over hyphen to set new start of option text
    }

    state->optopt = *state->next_char; // Report current option to caller if return value is discarded
    rval = state->optopt;

    // Search optstring for the current option
    const char *opt_def = strchr(optstring, state->optopt);

    if(opt_def) {

      if(opt_def[1] == ':') { // This option takes an argument
        // Assume the argument follows immediately without space
        state->optarg = &state->next_char[1];

        if(state->optarg[0] == '\0') { // There is no arg included in this option

          if(opt_def[2] != ':') { // Mandatory argument
            state->optind++; // Look for argument in next position
            if(CUR_OPT) {
              state->optarg = CUR_OPT;
            } else { // Exhausted all options without finding an argument
              state->optarg = NULL;

              if(state->report_errors)
                fprintf(stderr, "ERROR: getopt_r() Missing required argument to option '-%c'\n",
                        state->optopt);

              // Leading ':' in optstring affects error condition but not error reporting
              rval = optstring[0] == ':' ? ERR_MISSING_OPT : ERR_UNKNOWN_OPT;
            }

          } else { // Optional argument (GNU extension using "::")
            // Check if the next arg doesn't begin with '-'
            state->optind++;
            if(CUR_OPT && CUR_OPT[0] != '-') { // We use this as the argument
              state->optarg = CUR_OPT;
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

      rval = ERR_UNKNOWN_OPT;
    }

    if(!state->next_char) { // All options in this arg are consumed
      state->optind++; // Work on next arg

    } else { // Prep for next option within this arg
      state->next_char++;
      if(*state->next_char == '\0') {
        state->optind++;
      }
    }
  }

  // Permute non-options
  if(non_opts > 0)
    permute_args((char **)argv, non_opts, non_opt_start, state);

  return rval;
}



static const OptionHelp *find_help(const char *name, const OptionHelp *opt_help) {
  for(int i = 0; opt_help[i].name; i++) {
    if(!strcmp(opt_help[i].name, name))
      return &opt_help[i];
  }

  return NULL;
}


static const struct option *find_long_option(char short_opt, const struct option *long_options) {
  for(int lo = 0; long_options[lo].name; lo++) {
    if(!long_options[lo].flag && long_options[lo].val == short_opt)
      return &long_options[lo];
  }

  return NULL;
}


static void print_help_detail(const OptionHelp *help, unsigned opt_len, unsigned max_prefix,
                              unsigned  max_columns, bool long_opt) {
  int prefix_len = opt_len;
  bool break_line = max_prefix + strlen(help->help) > max_columns; // Check if it fits

#define LINE_BREAK_INDENT   16

  if(help->arg_name) {
    prefix_len += strlen(help->arg_name) + 1;

    if(!break_line) {
      printf("%c%s%*s  %s", long_opt ? '=':' ', help->arg_name, max_prefix-prefix_len, "", help->help);
    } else {  // Break help text to new line
      printf("%c%s\n", long_opt ? '=':' ', help->arg_name);
      printf("%*s%s", LINE_BREAK_INDENT, "", help->help);
    }

  } else {  // No argument string
    if(!break_line) {
      printf("%*s  %s", max_prefix-prefix_len, "", help->help);
    } else {  // Break help text to new line
      printf("\n%*s%s", LINE_BREAK_INDENT, "", help->help);
    }
  }
}


static void append_option(char *buf, AppendRange *buf_r, int indent, int max_columns,
                          const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);

    if(range_cat_vfmt(buf_r, fmt, args) < 0) {  // Won't fit
      puts(buf);

      // Start a new line
      range_init(buf_r, buf, max_columns+1);
      range_cat_fmt(buf_r, "%*s", indent, "");

      // Retry
      va_end(args);
      va_start(args, fmt);
      range_cat_vfmt(buf_r, fmt, args);
    }

    va_end(args);
}



/*
Print generated command usage

This prints a usage summary of command options followed by a detailed list
of options with descriptive help.

:c:data:`optstring` can be an empty string if only long or positional options are used.
:c:data:`long_options` and :c:data:`opt_help` must be terminated by a NULL element.
:c:data:`positional_args` must be terminated by an empty string.

The :c:data:`opt_help` array has definitions of help text and an argument description
for each option. Long options are identified by a matching string to the one in
:c:data:`long_options`. Short options are identified by a matching single character string.

The :c:data:`arg_name` field in the help definitions should be wrapped in angle brackets
for variable options and curly braces for fixed choices:

  * "<file path>"
  * "{red|blue|green}"

Args:
  app_name:         Application name
  optstring:        Short options as used by :c:func:`getopt_r`
  long_options:     Optional long options as used by :c:func:`getopt_long_r`
  positional_args:  Optional array of strings for positional args
  opt_help:         Array of help definitions for the options
  max_columns:      Maximum columns for usage output
*/
void print_command_usage(const char *app_name, const char *optstring, const struct option *long_options,
                        const char **positional_args, const OptionHelp *opt_help, int max_columns) {

  // https://stackoverflow.com/questions/9725675/is-there-a-standard-format-for-command-line-shell-help-text

  char short_opt[2] = {0};

  printf(A_BOLD "Usage: " A_NONE "%s", app_name);
  int indent = 7 + strlen(app_name);

  // Print short summary
  char *buf = malloc(max_columns+1);
  AppendRange buf_r;
  if(buf) {
    buf[0] = '\0';
    range_init(&buf_r, buf, max_columns+1 - indent); // Shorten first line
    // Long options
    if(long_options) {
      for(int lo = 0; long_options[lo].name; lo++) {
        const OptionHelp *help = find_help(long_options[lo].name, opt_help);
        bool required = help && (help->flags & OPT_REQUIRED);

        const char *fmt;
        if(!long_options[lo].flag && isgraph(long_options[lo].val)) { // Have short option
          fmt = required ? " -%c|--%s%s" : " [-%c|--%s%s]";
          append_option(buf, &buf_r, indent, max_columns,
                        fmt, long_options[lo].val, long_options[lo].name,
                        long_options[lo].has_arg != no_argument ? "=<>" : "");

        } else {  // No short option
          fmt = required ? " --%s%s" : " [--%s%s]";
          append_option(buf, &buf_r, indent, max_columns,
                        fmt, long_options[lo].name,
                        long_options[lo].has_arg != no_argument ? "=<>" : "");
        }

      }
    }

    // Short options
    for(int i = 0; optstring[i] != '\0'; i++) {
      if(long_options && find_long_option(optstring[i], long_options)) continue;
      if(optstring[i] == ':') continue;

      short_opt[0] = optstring[i];
      const OptionHelp *help = find_help(short_opt, opt_help);
      bool required = help && (help->flags & OPT_REQUIRED);

      const char *fmt = required ? " -%c%s" : " [-%c%s]";
      append_option(buf, &buf_r, indent, max_columns, fmt, optstring[i],
                    optstring[i+1] == ':'? " <>" : "");
    }

    // Positional args
    if(positional_args) {
      int i = 0;
      while(positional_args[i][0] != '\0') {
        const OptionHelp *help = find_help(positional_args[i], opt_help);
        bool required = help && (help->flags & OPT_REQUIRED);

        const char *fmt = required ? " %s" : " [%s]";
        append_option(buf, &buf_r, indent, max_columns, fmt, positional_args[i]);
        i++;
      }
    }
  }
  if(strlen(buf) > 0)
    puts(buf);
  free(buf);
  puts("");


  int max_prefix = 4; // ("  -x")

  if(long_options) {
    // Find the longest long option + arg name combo
    for(int lo = 0; long_options[lo].name; lo++) {
      int prefix_len = 6 + 2 + strlen(long_options[lo].name);

      const OptionHelp *help = find_help(long_options[lo].name, opt_help);
      if(help && help->arg_name)
        prefix_len += strlen(help->arg_name) + 1;

      max_prefix = (prefix_len > max_prefix) ? prefix_len : max_prefix;
    }
  }

  // Find the longest short option + arg name combo
  for(int i = 0; optstring[i] != '\0'; i++) {
    if(long_options && find_long_option(optstring[i], long_options)) continue;
    if(optstring[i] == ':') continue;

    int prefix_len = 4;

    short_opt[0] = optstring[i];
    const OptionHelp *help = find_help(short_opt, opt_help);
    if(help && help->arg_name)
      prefix_len += strlen(help->arg_name) + 1;

    max_prefix = (prefix_len > max_prefix) ? prefix_len : max_prefix;
  }


  // Print detailed long options
  if(long_options) {
    for(int lo = 0; long_options[lo].name; lo++) {
      if(!long_options[lo].flag && isgraph(long_options[lo].val)) {  // Have short option
        printf("  -%c, --%s", long_options[lo].val, long_options[lo].name);
      } else { // No short option
        printf("      --%s", long_options[lo].name);
      }
      int opt_len = 6 + 2 + strlen(long_options[lo].name);

      const OptionHelp *help = find_help(long_options[lo].name, opt_help);

      if(help) {
        print_help_detail(help, opt_len, max_prefix, max_columns, /*long_opt*/true);
      } else {  // No help. Show if arg is present
        if(long_options[lo].has_arg != no_argument)
          printf("=<%s>", long_options[lo].has_arg == required_argument ? "required" : "optional");
      }

      puts("");
    }
  }

  // Print detailed short options
  for(int i = 0; optstring[i] != '\0'; i++) {
    if(long_options && find_long_option(optstring[i], long_options)) continue;
    if(optstring[i] == ':') continue;

    printf("  -%c", optstring[i]);
    int opt_len = 4;

    short_opt[0] = optstring[i];  // Convert to single char string
    const OptionHelp *help = find_help(short_opt, opt_help);
    if(help) {
      print_help_detail(help, opt_len, max_prefix, max_columns, /*long_opt*/false);
    } else {
      if(optstring[i+1] == ':')
        printf(" <%s>", (optstring[i+2]== ':') ? "optional" : "required");
    }
    puts("");
  }

  // Print detailed positional args
  if(positional_args) {
    int i = 0;
    while(positional_args[i][0] != '\0') {
      printf("  %s", positional_args[i]);
      int opt_len = 2 + strlen(positional_args[i]);

      const OptionHelp *help = find_help(positional_args[i], opt_help);
      if(help)
        print_help_detail(help, opt_len, max_prefix, max_columns, /*long_opt*/false);

      puts("");
      i++;
    }
  }
}



#ifdef TEST_GETOPT

int main(int argc, char *argv[]) {
  GetoptState state = {0};
  state.report_errors = true;
  state.permute_args = true;

  int c;

  static int delta_flag = 0;
  static const struct option longopts[] = {
    {"alpha", no_argument, NULL, 'a'},
    {"beta", required_argument, NULL, 'b'},
    {"gamma", optional_argument, NULL, 'c'},
    {"delta", required_argument, &delta_flag, 42},
    {0}
  };

  while((c = getopt_long_r(argv, "ab:c::", longopts, &state)) != -1) {
    puts("argv:");
    for(int i = 0; argv[i]; i++) {
      printf("\t'%s'", argv[i]);
      if(i == state.optind)
        puts(" <--");
      else
        puts("");
    }

    switch(c) {
    case 'a':
      puts("Option 'a'");
      break;
    case 'b':
      printf("Option 'b' = '%s'\n", state.optarg);
      break;
    case 'c':
      printf("Option 'c' = '%s'\n", state.optarg);
      break;
    case 0:
      printf("Long option %d (%s):\n", state.long_index, longopts[state.long_index].name);
      if(delta_flag)
        printf("Option delta: %d = '%s'\n", delta_flag, state.optarg);
      break;

    default:
    case ':':
    case '?':
      printf("Error code: '%c'\n", c);
      return -3;
      break;
    }
  }

  printf("Final argv: %d\n", state.optind);
  for(int i = 0; argv[i]; i++) {
    printf("\t'%s'", argv[i]);
    if(i == state.optind)
      puts(" <--");
    else
      puts("");
  }


  return 0;
}

#endif
