#include <stdio.h>
#include <string.h>

#include "evfs.h"

#include "evfs/stdio_fs.h"
#include "evfs/shim/shim_trace.h"
#include "evfs/shim/shim_jail.h"

#include "evfs/util/getopt_r.h"


// Callback for trace shim
int treport(const char *buf, void *ctx) {
  fputs(buf, (FILE *)ctx);
  return 0;
}


int main(int argc, char *argv[]) {
  printf("Jail test\n");

  evfs_init();


  // Process command line
  struct s_options {
    bool show_trace;
  } options;


  options.show_trace = false;

  GetoptState state = {.report_errors = true};

  int c;
  while((c = getopt_r(argv, "th", &state)) != -1) {
    switch(c) {
    case 't':
      options.show_trace = true;
      break;
    default:
    case 'h':
    case ':':
    case '?':
      {
        // Get the base name for the executable
        StringRange base;
        evfs_path_basename(argv[0], &base);

        printf("Usage: %.*s [-o write|read|trunc|r] [-t] [-h]\n", RANGE_FMT(&base));
        puts("  -o <op>\ttest operation on container");
        puts("  -t     \tshow EVFS tracing");
        puts("  -h     \tdisplay this help and exit");
      }
      return 0;
      break;
    }
  }




  int status;

  evfs_register_stdio(/*default*/ false);
  if(options.show_trace) {
    evfs_register_trace("t_stdio", "stdio", treport, stderr, /*default*/ false);
    status = evfs_register_jail("jail", "t_stdio", "./jail_fs", /*default*/ true);
  } else {
    status = evfs_register_jail("jail", "stdio", "./jail_fs", /*default*/ true);
  }

  if(status != EVFS_OK) {
    printf("Failed to register shim: %s\n", evfs_err_name(status));
    return 1;
  }


  evfs_make_dir_ex("jail_fs", "stdio");


  EvfsFile *fh;
  // Escape from jail won't work
  status = evfs_open("../hello.txt", &fh, EVFS_RDWR | EVFS_OVERWRITE);

  if(status == EVFS_OK) {

    char buf[100];

    printf("Write to file\n");
    sprintf(buf, "Hello, world\n");
    evfs_file_write(fh, buf, strlen(buf)); 

    evfs_file_rewind(fh);
    evfs_file_sync(fh);

    if(evfs_existing_file("copy.txt")) {
      printf("Delete existing copy\n");
      evfs_delete("copy.txt");
    }

    printf("Copy file\n");
    evfs_copy_to_file("copy.txt", fh, NULL, 1024);

    evfs_file_close(fh);

    evfs_make_dir("subdir");
    printf("Set current dir\n");
    evfs_set_cur_dir("subdir");

    char path[EVFS_MAX_PATH];
    StringRange path_r = RANGE_FROM_ARRAY(path);
    evfs_get_cur_dir(&path_r);
    printf("Current dir: '%s'\n", path);

  }

  return 0;
}
