#include <stdio.h>
#include <string.h>

#include "evfs.h"

#include "evfs/stdio_fs.h"
#include "evfs/tar_fs.h"
#include "evfs/tar_rsrc_fs.h"
#include "evfs/shim/shim_trace.h"

#include "evfs/util/getopt_r.h"


// Resource with test data
#include "test_tar.h"


// Callback for trace shim
int treport(const char *buf, void *ctx) {
  fputs(buf, (FILE *)ctx);
  return 0;
}




int main(int argc, char *argv[]) {
  printf("TAR file test\n");

  evfs_init();


  // Process command line
  struct s_options {
    bool show_trace;
    const char *tar_file;
  } options;


  options.show_trace = false;
  options.tar_file = NULL;

  GetoptState state = {.report_errors = true};

  int c;
  while((c = getopt_r(argv, "tf:h", &state)) != -1) {
    switch(c) {
    case 't':
      options.show_trace = true;
      break;
    case 'f':
      options.tar_file = state.optarg;
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

  if(options.tar_file) {
    printf("Loading file: %s\n", options.tar_file);
    EvfsFile *tar_file;
    status = evfs_open(options.tar_file, &tar_file, EVFS_READ);

    evfs_register_tar_fs("tarfs", tar_file, /*default*/ true);

  } else { // Use compiled resource
    puts("Loading resource");
    evfs_register_tar_rsrc_fs("tarfs", test_tar, test_tar_len, /*default*/ true);
  }

  evfs_register_trace("t_tarfs", "tarfs", treport, stderr, /*default*/ true);


  EvfsFile *fh;
  //evfs_set_cur_dir("/src/util");
  status = evfs_open("littlefs_fs.c", &fh, EVFS_READ);
  char buf[256] = {0};

  for(int i = 0; i < 3; i++) {
    evfs_file_read(fh, buf, sizeof(buf)-1);
    //dump_array(buf, sizeof(buf));
  }

#if 0
  uint8_t *data;
  if(evfs_file_ctrl(fh, EVFS_CMD_GET_RSRC_ADDR, &data) == EVFS_OK)
    dump_array(data, evfs_file_size(fh));
#endif

  evfs_file_close(fh);


  return 0;
}
