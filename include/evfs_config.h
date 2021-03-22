/* SPDX-License-Identifier: MIT
Copyright 2020 Kevin Thibedeau
(kevin 'period' thibedeau 'at' gmail 'punto' com)

See LICENSE in the EVFS project root for details
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



// ******************** Library behavior ********************

// Define to make evfs_off_t 64-bits. When evfs_off_t is the default 32-bits, the max
// supported file size will be 2GiB.
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

// Shared buffers for the tar FS and tar resource FS
#define EVFS_USE_TARFS_SHARED_BUFFER

#endif // EVFS_CONFIG_H
