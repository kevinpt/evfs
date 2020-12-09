#include <stdio.h>
#include <string.h>
#include "evfs.h"
#include "evfs/stdio_fs.h"
#include "evfs/littlefs_fs.h"
#include "lfs.h"
#include "evfs/littlefs_image.h"

int main() {
  lfs_t lfs;
  LittlefsImage s_lfs_img = {0};

  #define KB  *1024UL
  #define LFS_VOL_SIZE   (512 KB)
  #define LFS_BLOCK_SIZE  4096

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
      .block_count    = LFS_VOL_SIZE / LFS_BLOCK_SIZE,
      .cache_size     = 1024,
      .lookahead_size = 16,
      .block_cycles   = -1, // Disable wear leveling  // 500,
  };


  evfs_init();

  evfs_register_stdio(/*default_vfs*/ true);

  littlefs_make_image("littlefs.img", &s_lfs_cfg);        // No effect if image exists
  littlefs_mount_image("littlefs.img", &s_lfs_cfg, &lfs); // Image stored on base filesystem

  // Add littlefs as default VFS
  evfs_register_littlefs("lfs", &lfs, /*default_vfs*/ true);

  // Access the image like a normal fielsystem

  EvfsFile *fh;
  evfs_open("hello.txt", &fh, EVFS_WRITE | EVFS_OVERWRITE);

  char buf[100];
  sprintf(buf, "Hello, world\n");
  evfs_file_write(fh, buf, strlen(buf));

  evfs_file_close(fh);

  littlefs_unmount_image(&lfs);

  evfs_unregister_all();

  return 0;
}
