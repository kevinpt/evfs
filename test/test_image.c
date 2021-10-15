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

  Demo test program for VFS images hosted on top of Stdio
------------------------------------------------------------------------------
*/

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#include "evfs.h"

#include "evfs/stdio_fs.h"
#include "evfs/shim/shim_trace.h"
#include "evfs/shim/shim_jail.h"

#include "ff.h"
#include "evfs/fatfs_image.h"
#include "evfs/fatfs_fs.h"

#include "lfs.h"
#include "evfs/littlefs_image.h"
#include "evfs/littlefs_fs.h"

#include "evfs/util/getopt_r.h"


#define KB  *1024UL
#define IMAGE_SIZE   (512 KB)


// Callback for trace shim
static int treport(const char *buf, void *ctx) {
  fputs(buf, (FILE *)ctx);
  return 0;
}


// Retrieve file metadata on VFSs that don't provide it all with evfs_dir_read()
static bool get_stat(const char *path, const char *fname, EvfsInfo *info, const char *vfs_name) {
  // Get the FS capabilities for stat
  //unsigned stat_fields = 0;
  //evfs_vfs_ctrl_ex(EVFS_CMD_GET_STAT_FIELDS, &stat_fields, vfs_name);

  char joined[EVFS_MAX_PATH];
  StringRange joined_r = RANGE_FROM_ARRAY(joined);
  evfs_path_join_str_ex(path, fname, &joined_r, vfs_name);

  if(evfs_stat_ex(joined, info, vfs_name) == EVFS_OK)
    return true;

  // Can't get stat
  return false;
}


// Print directory listing for a path
static void print_dir(const char *path, const char *vfs_name) {
  EvfsDir *dir;
  EvfsInfo info;

  char time_buf[32];
  printf("\nDirectory contents of '%s' on %s VFS\n", path, vfs_name ? vfs_name : evfs_default_vfs_name());
  int status = evfs_open_dir_ex(path, &dir, vfs_name);
  if(status == EVFS_OK) {
      // Get the FS capabilities for dir entries
    unsigned dir_fields = 0;
    evfs_vfs_ctrl_ex(EVFS_CMD_GET_DIR_FIELDS, &dir_fields, vfs_name);

    // Get the FS capabilities for stat
    unsigned stat_fields = 0;
    evfs_vfs_ctrl_ex(EVFS_CMD_GET_STAT_FIELDS, &stat_fields, vfs_name);

    stat_fields = stat_fields & ~dir_fields; // Stat fields not provided by read_dir


    while(evfs_dir_read(dir, &info) != EVFS_DONE) {
      if(stat_fields & EVFS_INFO_MTIME || stat_fields & EVFS_INFO_SIZE) {
        EvfsInfo sinfo;
        if(get_stat(path, info.name, &sinfo, vfs_name)) {
          info.mtime = sinfo.mtime;
          info.size = sinfo.size;
        }
      }

      // Format time if available on this FS
      if(info.mtime > 0) {
        struct tm mtime;
        localtime_r(&info.mtime, &mtime);
        strftime(time_buf, COUNT_OF(time_buf), "%b %d %Y", &mtime);
      } else { // No time
        strcpy(time_buf, "--");
      }

      printf("  %c  % 8ld  %11s  %s\n", (info.type & EVFS_FILE_DIR) ? 'd' : ' ', (int64_t)info.size, time_buf, info.name);
    }
    evfs_dir_close(dir);
  } else {
    printf("\tERROR: %s\n", evfs_err_name(status));
  }
}



int main(int argc, char *argv[]) {

  int c;

  printf("EVFS image demo\n\n");

  evfs_init();

  // Process command line

  bool use_littlefs = false;
  bool show_trace = false;

  GetoptState state = {.report_errors = true};

  while((c = getopt_r(argv, "f:th", &state)) != -1) {
    switch(c) {
    case 'f':
      if(state.optarg) {
        if(!strcmp(state.optarg, "littlefs")) {
          use_littlefs = true;
        } else if(!strcmp(state.optarg, "fatfs")) {
          use_littlefs = false;
        }
      }
      break;
    case 't':
      show_trace = true;
      break;
    default:
    case 'h':
    case ':':
    case '?':
      {
        // Get the base name for the executable
        StringRange base;
        evfs_path_basename(argv[0], &base);

        printf("Usage: %.*s [-f fatfs|littlefs] [-t] [-h]\n", RANGE_FMT(&base));
        puts("  -f <fs>\tselect filesystem for image");
        puts("  -t     \tshow EVFS tracing");
        puts("  -h     \tdisplay this help and exit");
      }
      return 0;
      break;
    }
  }

  
  printf("Using %s for image file\n", use_littlefs ? "littlefs" : "FatFs");


// Configure the selected image filesystem


  // We need a base FS to access the image file_name
  // EVFS can do this for us
  evfs_register_stdio(/*default*/ true);
  //evfs_register_trace("t_stdio", "stdio", treport, stderr, /*default*/ true);
  //evfs_register_jail("jail", "stdio", "./foo", /*default*/ true);
  //evfs_register_trace("t_jail", "jail", treport, stderr, /*default*/ true);



  uint8_t pdrv = 0; // FatFs volume number

#define LFS_BLOCK_SIZE  4096
  lfs_t lfs;
  LittlefsImage s_lfs_img = {0};
  struct lfs_config s_lfs_cfg = {
      // Must have a valid LittlefsImage object for context data
      .context = &s_lfs_img,

      // Block device operations
      .read  = littlefs_image_read,
      .prog  = littlefs_image_prog,
      .erase = littlefs_image_erase,
      .sync  = littlefs_image_sync,

      // Block device configuration
      .read_size      = 16,
      .prog_size      = 16,
      .block_size     = LFS_BLOCK_SIZE,
      .block_count    = IMAGE_SIZE / LFS_BLOCK_SIZE,
      .cache_size     = 1024,
      .lookahead_size = 16,
      .block_cycles   = -1, // Disable wear leveling  // 500,
  };


  if(use_littlefs) {
    const char littlefs_image_fname[] = "test_littlefs.img";

    littlefs_make_image(littlefs_image_fname, &s_lfs_cfg);
    if(littlefs_mount_image(littlefs_image_fname, &s_lfs_cfg, &lfs) != EVFS_OK) {
      printf("ERROR: Failed to mount littlefs image\n");
      return -1;
    }

    // LFS image is ready. Register it as a new filesystem
    evfs_register_littlefs("image", &lfs, /*default*/ true);


  } else { // FatFs
    const char fatfs_image_fname[] = "test_fatfs.img";

    fatfs_make_image(fatfs_image_fname, pdrv, IMAGE_SIZE);
    if(fatfs_mount_image(fatfs_image_fname, pdrv) != EVFS_OK) {
      printf("ERROR: Failed to mount FatFs image\n");
      return -1;
    }

    // Image is ready. Register it as a new filesystem
    evfs_register_fatfs("image", pdrv, /*default*/ true);
  }


  // Optional tracing for the mounted image
  if(show_trace)
    evfs_register_trace("t_image", "image", treport, stderr, /*default*/ true);


  //unsigned readonly = 1;
  //evfs_vfs_ctrl_ex(EVFS_CMD_SET_READONLY, &readonly, "stdio"); // Disable writes to FS hosting the image


  // From this point, EVFS operations work against the image filesystem as it
  // (or its tracing wrapper) is the default.
  // The stdio FS is still accessible using the *_ex() variants


  // Create a file and update its content
  EvfsFile *fh;

  const char boot_count_fname[] = "/boot.bin";


  int status = evfs_open(boot_count_fname, &fh, EVFS_RDWR | EVFS_OPEN_OR_NEW);
  int boot_count = 0;
  if(status == EVFS_OK) {
    evfs_file_read(fh, &boot_count, sizeof(boot_count));

    boot_count++;
    evfs_file_rewind(fh);
    evfs_file_write(fh, &boot_count, sizeof(boot_count));
    evfs_file_close(fh);
  }

  printf("Boot count from '%s' is %d\n", boot_count_fname, boot_count);


  // Make some directories
  evfs_make_path("/a/b/c");
  evfs_set_cur_dir("/a/b/c");
  evfs_make_dir("d");


  // Do some path manipulation
  evfs_set_cur_dir("/");
  const char base_dir[] = "foo/bar/baz";
  const char file_path[] = "../hello_world.txt";

  StringRange head = RANGE_FROM_ARRAY(base_dir);
  StringRange tail = RANGE_FROM_ARRAY(file_path);

  char joined[256];
  StringRange joined_r = RANGE_FROM_ARRAY(joined);

  evfs_path_join(&head, &tail, &joined_r);

  printf("\nJoined path:     '%s' + '%s' --> '%s'\n", base_dir, file_path, joined);

  evfs_path_normalize(joined, &joined_r); // Overwrite the joined path
  printf("Normalized path: '%s'\n", joined);

  evfs_path_absolute(joined, &joined_r); // Overwrite the normalized path
  printf("Absolute path:   '%s'\n", joined);


  // Create the directories for this path
  evfs_path_dirname(joined, &head);
  printf("Creating directories: '%.*s'\n", RANGE_FMT(&head));
  evfs_make_path_range(&head);

  // Create the file
  status = evfs_open(joined, &fh, EVFS_WRITE | EVFS_OVERWRITE);
  if(status == EVFS_OK) {
    const char buf[] = "Hello, world\n";
    evfs_file_write(fh, buf, COUNT_OF(buf));
    evfs_file_truncate(fh, 10);

    printf("Created file: '%s'\n", joined);
    evfs_file_close(fh);
  }


  // Print directory listings
  unsigned no_dir_dots = 1;
  evfs_vfs_ctrl(EVFS_CMD_SET_NO_DIR_DOTS, &no_dir_dots); // Filter out "." and ".." in evfs_dir_read()

  print_dir("/", NULL);

  no_dir_dots = 0;
  evfs_vfs_ctrl(EVFS_CMD_SET_NO_DIR_DOTS, &no_dir_dots);

  print_dir("/foo/bar", NULL);
  

  // Access a named VFS
  no_dir_dots = 1;
  evfs_vfs_ctrl_ex(EVFS_CMD_SET_NO_DIR_DOTS, &no_dir_dots, "stdio");

  char cur_dir[EVFS_MAX_PATH];
  StringRange cur_dir_r = RANGE_FROM_ARRAY(cur_dir);
  if(evfs_get_cur_dir_ex(&cur_dir_r, "stdio") == EVFS_OK) {
    print_dir(cur_dir, "stdio");
  }


  // Cleanup
  if(use_littlefs) {
    littlefs_unmount_image(&lfs);
  } else { // FatFs
    fatfs_unmount_image(pdrv);
  }

/*
  char *paths[] = {
    "/",
    "\\\\\\//////////////foo\\bar/../baz\\",
    "/foo////./././././///\\/\\/\\/\\/bar",
    "foo/../..",
    "foo/../../../bar",
    "foo",
    "../../foo/./bar.baz",
    "/../../foo",
    ""
  };

  int cur_path = 0;
  //Evfs *vfs = evfs_find_vfs("stdio");
  char long_path[EVFS_MAX_PATH] = {0};
  //AppendRange long_path_r = RANGE_FROM_ARRAY(long_path);

  while(paths[cur_path][0] != '\0') {
    init_range(&cur_dir_r, cur_dir, COUNT_OF(cur_dir));
    evfs_path_normalize(paths[cur_path], &cur_dir_r);

    //init_range(&long_path_r, long_path, COUNT_OF(long_path));
    //path_normalize_long(vfs, paths[cur_path], &long_path_r);

    printf("'%s'\t\t--> '%s'  '%s'\n", paths[cur_path], cur_dir, long_path);
    cur_path++;
  }
*/

  // Release any resources we were using
  evfs_unregister_all();

  return 0;
}

