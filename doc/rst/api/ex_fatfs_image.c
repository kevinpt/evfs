#include <stdio.h>
#include <string.h>
#include "evfs.h"
#include "evfs/stdio_fs.h"
#include "evfs/fatfs_fs.h"
#include "ff.h"
#include "evfs/fatfs_image.h"

int main() {
  evfs_init();

  evfs_register_stdio(/*default_vfs*/ true);

  // <<Configure FatFs volume handling via callbacks>>
  int pdrv = 0; // FatFs volume number
  fatfs_make_image("fatfs.img", pdrv, 512*1024); // No effect if image exists
  fatfs_mount_image("fatfs.img", pdrv);          // Image stored on base filesystem

  // Add FatFs as default VFS
  evfs_register_fatfs("vol0", pdrv, /*default_vfs*/ true);

  // Access the image like a normal fielsystem

  EvfsFile *fh;
  evfs_open("hello.txt", &fh, EVFS_WRITE | EVFS_OVERWRITE);

  char buf[100];
  sprintf(buf, "Hello, world\n");
  evfs_file_write(fh, buf, strlen(buf));

  evfs_file_close(fh);

  fatfs_unmount_image(pdrv);

  evfs_unregister_all();

  return 0;
}
