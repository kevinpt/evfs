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
Embedded Virtual Filesystem

  Configuration settings
------------------------------------------------------------------------------
*/

#ifndef EVFS_CONFIG_H
#define EVFS_CONFIG_H

// ******************** Debug settings ********************

// Force debugging on
//#define EVFS_DEBUG  1

// Use EVFS_ASSERT_LEVEL to control debug reporting level for ASSERT() macro:
//   0:   Always return 0 (assertion ignored)
//   1:   Silent assertion check
//   2:   Check assertion with diagnostic output
//   3:   Check assertion with diagnostic output and abort() when EVFS_DEBUG == 1
#define EVFS_ASSERT_LEVEL  2


// ******************** Path handling ********************

// Separator characters for path parsing
#define EVFS_PATH_SEPS  "/\\"

// Separator character for path construction and normalization
#define EVFS_DIR_SEP    '/'

// Longest supported paths
#define EVFS_MAX_PATH   256

// Allow paths longer than EVFS_MAX_PATH from evfs_path_join() and evfs_path_absolute()
//#define ALLOW_LONG_PATHS



// ******************** Thread support ********************

// Use threading library to implement locks on shared resources
#define EVFS_USE_THREADING

#ifdef EVFS_USE_THREADING
// Select the threading API to use:

// Use C11 thread API
#  define EVFS_USE_C11_THREADS

// Use POSIX threads
//#  define EVFS_USE_PTHREADS
#endif


// ******************** Library behavior ********************

// Define to make evfs_off_t 64-bits. When evfs_off_t is 32-bits the max supported
// file size will be 2GiB.
//#define EVFS_FILE_OFFSET_BITS  64

// Install an atexit() handler for shutdown
#define EVFS_USE_ATEXIT

// Color output for trace shim messages and error diagnostics
#define EVFS_USE_ANSI_COLOR


// ******************** VFS options ********************

// The Stdio VFS needs to use POSIX calls for directory access operations.
// If this is not defined some of the EVFS API will be non-functional on Stdio.
#define EVFS_USE_STDIO_POSIX


// The Littlefs driver constructs absolute paths before passing them into the
// lfs API. Define this to use a common shared buffer in place of malloc.
#define EVFS_USE_LITTLEFS_SHARED_BUFFER

// The log rotate shim supports a common shared buffer for path operations.
#define EVFS_USE_ROTATE_SHARED_BUFFER

#endif // EVFS_CONFIG_H
