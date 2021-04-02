#include <stdio.h>
#include <string.h>

#include "evfs.h"

#include "evfs/stdio_fs.h"
#include "evfs/romfs_fs.h"
#include "evfs/shim/shim_trace.h"

#include "evfs/util/getopt_r.h"

#include "test_romfs.h"
#include "hex_dump.h"


// Callback for trace shim
int treport(const char *buf, void *ctx) {
  fputs(buf, (FILE *)ctx);
  return 0;
}




int main(int argc, char *argv[]) {
  printf("Romfs file test\n");

  evfs_init();


  // Process command line
  struct s_options {
    bool show_trace;
    const char *image_file;
  } options;


  options.show_trace = false;
  options.image_file = NULL;

  GetoptState state = {.report_errors = true};

  int c;
  while((c = getopt_r(argv, "tf:h", &state)) != -1) {
    switch(c) {
    case 't':
      options.show_trace = true;
      break;
    case 'f':
      options.image_file = state.optarg;
      break;
    default:
    case 'h':
    case ':':
    case '?':
      {
        // Get the base name for the executable
        StringRange base;
        evfs_path_basename(argv[0], &base);

        printf("Usage: %.*s [-t] [-h]\n", RANGE_FMT(&base));
        puts("  -t     \tshow EVFS tracing");
        puts("  -h     \tdisplay this help and exit");
      }
      return 0;
      break;
    }
  }




  int status;

  status = evfs_register_stdio(/*default*/ true);
  if(options.show_trace) {
    status = evfs_register_trace("t_stdio", "stdio", treport, stderr, /*default*/ true);
  }

  if(status != EVFS_OK) {
    printf("Failed to register: %s\n", evfs_err_name(status));
    return 1;
  }

  if(options.image_file) {
    printf("Loading file: %s\n", options.image_file);
    EvfsFile *image;
    status = evfs_open(options.image_file, &image, EVFS_READ);
    if(status != EVFS_OK) {
      printf("No image\n");
      exit(1);
    }

    evfs_register_romfs("romfs", image, /*default*/ true);

  } else { // Use compiled resource
    puts("Loading resource");

    evfs_register_rsrc_romfs("romfs", test_romfs, test_romfs_len, /*default*/ true);
  }


  evfs_register_trace("t_romfs", "romfs", treport, stderr, /*default*/ true);


  // Test file reading
  EvfsFile *fh;
  evfs_set_cur_dir("/evfs/shim");
  status = evfs_open("../util/dhash.h", &fh, EVFS_READ);

  char buf[256+1] = {0};

  for(int i = 0; i < 1; i++) {
    memset(buf, 0xAA, sizeof(buf));
    ptrdiff_t read_bytes = evfs_file_read(fh, buf, sizeof(buf)-1);
    printf("Read %ld bytes\n", read_bytes);
    if(read_bytes > 0)
      dump_array((uint8_t *)buf, read_bytes);
    else
      break;
  }


  evfs_file_close(fh);


  // Test dir access

  unsigned no_dir_dots = 1;
  evfs_vfs_ctrl(EVFS_CMD_SET_NO_DIR_DOTS, &no_dir_dots); // Filter out "." and ".." in evfs_dir_read()


  EvfsDir *dh;
  EvfsInfo info;
  evfs_open_dir("/evfs", &dh);
/*  printf("Read dir:\n");
  do {
    status = evfs_dir_read(dh, &info);

    if(status == EVFS_OK)
      printf("  '%s'  %d  %c\n", info.name, info.size, info.type & EVFS_FILE_DIR ? 'D' : ' ');
  } while(status != EVFS_DONE);


  evfs_dir_rewind(dh);
  do {
    status = evfs_dir_read(dh, &info);

    if(status == EVFS_OK)
      printf("  '%s'  %d  %c\n", info.name, info.size, info.type & EVFS_FILE_DIR ? 'D' : ' ');
  } while(status != EVFS_DONE);
*/

  evfs_dir_close(dh);

  // Test stat

  evfs_set_cur_dir("/evfs");

  evfs_stat("stdio_fs.h", &info);
  printf("Stat: %d  %c\n", info.size, info.type & EVFS_FILE_DIR ? 'D' : 'f');

  evfs_set_cur_dir("/");

  status = evfs_stat("/", &info);
  printf("Stat: %d  %d  %c\n", status, info.size, info.type & EVFS_FILE_DIR ? 'D' : 'f');

  return 0;
}
