================
EVFS library API
================

The EVFS library has a number of sub-components that are described in the following sections:

* :doc:`Configuring EVFS <configure>`
* :doc:`Path handling <paths>`
* :doc:`Filesystems <filesystems>`
* :doc:`Shims <shims>`


Basic usage
-----------

Your first task is to initialize EVFS with the set of filesystems and shims you want to use. This is described below in more detail but we'll start with this skeleton:

.. code-block:: c

  #include "evfs.h"
  #include "evfs/stdio_fs.h"

  int main() {

    evfs_init();
    evfs_register_stdio(/*default_vfs*/ true);

    ...


From this point you have access to the C stdio filesystem using EVFS. You can perform filesystem level operations using the top level :ref:`filesystem access functions <fs-access>`. Among them are :c:func:`evfs_open_ex` and :c:func:`evfs_open_dir_ex` which create :c:type:`EvfsFile` and :c:type:`EvfsDir` objects respectively. These objects each have a set of methods for operating on the underlying :ref:`file <file-methods>` or :ref:`directory <dir-methods>`.

Most of the library functions return an integer error code. Success is indicated by :c:macro:`EVFS_OK` which is 0. Error conditions are always negative. Some functions will have additional positive values for non-error conditions. You can convert error codes into a string using :c:func:`evfs_err_name`:

.. code-block:: c

  EvfsFile *fh;
  int status = evfs_open("foobar/test.txt", &fh, EVFS_READ);
  printf("evfs_open() returned '%s'\n", evfs_err_name(status));

Access mode flags
~~~~~~~~~~~~~~~~~

When you open a file you must supply a set of flags to identify what mode you want to open the file in. EVFS provides the following set of flags:

================ =============================================
EVFS_READ         Open in read mode
EVFS_WRITE        Open in write mode
EVFS_RDWR         Combination of EVFS_READ and EVFS_WRITE
EVFS_OPEN_OR_NEW  Open an existing file or create a new one
EVFS_NO_EXIST     Do *not* open if the file exists
EVFS_OVERWRITE    Truncate all existing file content
EVFS_APPEND       Append writes to the end of file
================ =============================================

These can be merged together in combination to achieve different results.

.. code-block:: c

  // Open a new file to write into
  status = evfs_open("file.txt", &fh, EVFS_WRITE | EVFS_NO_EXIST);

  // Open file to append data and read back its content
  status = evfs_open("file.txt", &fh, EVFS_RDWR | EVFS_APPEND);

Files are always opened in binary mode for every filesystem. There is no special handling of newline characters.


To read and write from a file you need to have a buffer to hold the data going in or out. Reads and writes may be partial so you should be prepared to repeat an operation if necessary.

.. code-block:: c

  status = evfs_open("file.txt", &fh, EVFS_RDWR);

  char buf[100];
  int read = evfs_file_read(fh, buf, sizeof(buf));
  if(read >= 0)
    printf("Read %d bytes from file\n", read);

  evfs_file_rewind(fh); // Go back to start of the file

  int wrote = evfs_file_write(fh, buf, sizeof(buf));
  if(wrote >= 0)
    printf("Wrote %d bytes to file\n", wrote);

  evfs_file_close(fh);


File metadata
~~~~~~~~~~~~~

EVFS uses a common :c:type:`EvfsInfo` struct to work with file and directory metadata. 

.. c:struct:: EvfsInfo

  Metadata for files and directories

  * :c:texpr:`char`       \*name - Name of a file or directory
  * :c:texpr:`time_t`      mtime - Modification time
  * :c:texpr:`evfs_off_t`  size  - File size
  * :c:texpr:`uint8_t`     type  - Object type (file or directory)

These info structs are returned by :c:func:`evfs_stat_ex` and :c:func:`evfs_dir_read`. Not all fields will be populated by these functions. Some data may be unavailable depending on the underlying filesystem. You can check which fields are valid by calling :c:func:`evfs_vfs_ctrl_ex` with the :c:macro:`EVFS_CMD_GET_STAT_FIELDS` and :c:macro:`EVFS_CMD_GET_DIR_FIELDS` commands. This will return a bitfield value where set bits correspond to the members of :c:type:`EvfsInfo` that are valid for the current VFS. Unused fields will always be 0.

.. code-block:: c

  // Query for supported stat fields
  unsigned stat_fields;
  evfs_vfs_ctrl_ex(EVFS_CMD_GET_STAT_FIELDS, &stat_fields, "stdio");

  // Get stat
  EvfsInfo info;
  evfs_stat_ex("/file.txt", &info, "stdio");

  // Check each supported field before access

  // Name is always present so this particular check isn't strictly required
  if(stat_fields & EVFS_INFO_NAME)  printf("File name is: %s\n", info.name);

  // Mtime is missing on filesystems without time metadata and systems that don't
  // report time in directory listings.
  if(stat_fields & EVFS_INFO_MTIME) {
    char mtime[30];
    ctime_r(info.mtime, mtime);
    printf("Last modified: %s\n", mtime);
  }


Directory listing
-----------------

Directories can be listed by opening a :c:type:`EVfsDir` with :c:func:`evfs_open_dir_ex`. The :ref:`directory access methods <dir-methods>` are then used to scan through the contents of a directory. :c:func:`evfs_dir_read` will return :c:macro:`EVFS_DONE` when there are no more entries to read in a directory.

.. code-block:: c

  EvfsDir *dh;
  evfs_open_dir("path/to/dir", &dh);

  EvfsInfo info;
  int status = evfs_dir_read(dh, &info);
  while(status != EVFS_DONE) {
    // Work with this entry

    ...

    status = evfs_dir_read(dh, &info);
  }

  evfs_dir_close(dh);

Directory entries are returned in whatever order the underlying filesystem produces them. Some filesystems may add entries for the current (".") and parent ("..") directories. These can be suppressed by configuring the VFS with an :c:macro:`EVFS_CMD_SET_NO_DIR_DOTS` command:

.. code-block:: c

  unsigned no_dots = 1;
  evfs_vfs_ctrl(EVFS_CMD_SET_NO_DIR_DOTS, &no_dots);


EVFS architecture
-----------------

EVFS uses a design that takes inspiration from the SQLite VFS driver mechanism. You can register multiple :doc:`filesystem interfaces <filesystems>` and stack optional :doc:`shims <shims>` on top of them to alter behavior.

.. figure:: /images/evfs_system.svg

Every VFS interface or shim is assigned a name used to refer to the VFS object at a later time. File and directory handles are opened against one of these named VFS objects. Shims must be registered on top of an existing interface or another shim. One of the VFS objects is designated as the default. All API functions with the "_ex" suffix will target the default if the :c:var:`vfs_name` argument is NULL.

EVFS does not directly interact with storage media. You need to have the filesystem driver required to access your storage. The necessary driver code is provided by your operating system or an auxilliary library outside of EVFS.


Mounting image files
--------------------

EVFS is capable of mounting filesystem images stored on a host filesystem and then accessing them as an independent VFS. To accomplish this you need to incorporate a filesystem driver into your application. This driver layer normally expects to interact with the storage device. You need to add an I/O adapter that translates low level storage operations into EVFS API calls on the opened image file. An EVFS filesystem interface is registered to the new filesystem driver. The EVFS API can then be used to access the contents of the image file as if it were a normal filesystem.

.. figure:: /images/evfs_images.svg


EVFS includes code that manages image files for the :ref:`FatFs <fatfs-fs>` and :ref:`littlefs <littlefs-fs>` filesystems.


.. _lib-mgmt:

Library management
------------------

The :c:func:`evfs_init` function must be called before anything can be done with EVFS. This ensures initialization of internal data structures has been performed. When threading is enabled it also creates a library-wide lock to protect accesss to the data structures. If :c:macro:`EVFS_USE_ATEXIT` is enabled in 'evfs_config.h', an :c:func:`atexit` handler will also be installed. If this isn't enabled you must call :c:func:`evfs_unregister_all` before the process exits to ensure that all VFSs have flushed their data and released any resources.

The mechanism for adding a new VFS or shim is to use :c:func:`evfs_register`. Normally you will not call this directly but instead call a wrapper function provided by each VFS or shim driver. This function performs a secondary purpose of changing the default status of a VFS. If the VFS already exists, the :c:var:`make_default` argument will change the VFS to the new default if true. If it is set false, another VFS will be found to serve as the new default.

.. code-block:: c

  evfs_register_stdio(/*default_vfs*/ true); // Add a filesystem
  // stdio is now the default

  evfs_register_trace("t_stdio", "stdio", report, stderr, /*default_vfs*/ true); // Add a debug tracing shim
  // Default is now the trace shim on top of stdio


  // Bypass the shim and make stdio the default again
  Evfs *stdio_vfs = evfs_find_vfs("stdio");
  evfs_register(stdio_vfs, /*make_default*/ true);


  // Remove default from stdio. "t_stdio" will become the new default.
  evfs_register(stdio_vfs, /*make_default*/ false);


You can unregister a VFS or shim at runtime using :c:func:`evfs_unregister`. You must ensure that no open file or directory handles exist that reference back to a VFS being unregistered. The library does not track this for you.




.. c:function:: void evfs_init(void)

  Initialize the EVFS library.



.. c:function:: Evfs *evfs_find_vfs(const char *vfs_name)

  Search for a VFS by name.

  :param vfs_name: VFS to search for

  :return: The matching VFS object or NULL



.. c:function:: const char *evfs_vfs_name(Evfs *vfs)

  Get the name of a VFS object.

  :param vfs: The VFS to get name from


  :return: The name this VFS is registered under



.. c:function:: const char *evfs_default_vfs_name(void)

  Get the name of the default VFS object.

  :return: The name of the default VFS



.. c:function:: int evfs_register(Evfs *vfs, bool make_default)

  Register a new VFS or change the default status of an existing VFS.

  If the VFS has already been registered the make_default argument will
  update the status of this VFS. If there is only one registered VFS, make_default is ignored.

  This will fail if :c:func:`evfs_init` hasn't been called.

  :param vfs:           The new VFS to register
  :param make_default:  Set this VFS to be default or not

  :return: EVFS_OK on success



.. c:function:: int evfs_unregister(Evfs *vfs)

  Unregister a VFS object.

  :param vfs: The VFS to remove

  :return: EVFS_OK on success



.. c:function:: void evfs_unregister_all(void)

  Unregister all registered VFS objects.





.. _fs-access:

Filesystem access
-----------------

The top level of the EVFS API involves functions that directly interact with a VFS to perform operations. You can open, delete, rename, copy, and query for the status on files and directories. The :c:func:`evfs_open_ex` and :c:func:`evfs_open_dir_ex` functions return a handle that is used to perform further operations on those entities.

Most of these functions have an "_ex" suffix indicating that their last parameter is an optional VFS name. All of these forms have a macro defined without the suffix that uses the default VFS. This is the same as passing NULL for the :c:var:`vfs_name` argument to the "_ex" forms.

.. code-block:: c

  EvfsFile *fh;
  evfs_open_ex(path, &fh, EVFS_READ, "stdio"); // Open on named VFS

  evfs_open_ex(path, &fh, EVFS_READ, NULL);    // Open on default VFS
  evfs_open(path, &fh, EVFS_READ);             // Same as above using macro


There is a general purpose interface for passing named operations to the VFS via :c:func:`evfs_vfs_ctrl_ex`. It takes a command ID and a command specific argument to pass and return values that can alter or query internal VFS behavior. For example, you can use the :c:macro:`EVFS_CMD_SET_READONLY` command to write protect a VFS from any EVFS functions. See the listing of commands in 'evfs.h' for what is available and the types they take as an argument.

.. code-block:: c

  unsigned readonly = 1;
  evfs_vfs_ctrl(EVFS_CMD_SET_READONLY, &readonly); // Make the default VFS readonly

  unsigned no_dots = 1;
  evfs_vfs_ctrl(EVFS_CMD_SET_NO_DIR_DOTS, &no_dots); // Strip "." and ".." from the output of evfs_dir_read()




.. c:function:: int evfs_open_ex(const char *path, EvfsFile **fh, int flags, const char *vfs_name)

  Open a file.

  :param path:     Filesystem path to the file
  :param fh:       Handle for successfully opened file
  :param flags:    Open mode flags
  :param vfs_name: VFS to work on. Use default VFS if NULL

  :return: EVFS_OK on success



.. c:function:: int evfs_stat_ex(const char *path, EvfsInfo *info, const char *vfs_name)

  Get file or directory status.

  Different VFS backends may only support a partial set of :c:type:`EvfsInfo` fields.
  Use the :c:macro:`EVFS_CMD_GET_STAT_FIELDS` command with :c:func:`evfs_vfs_ctrl_ex` to query
  which fields are valid on a particular VFS. Unsupported fields will be 0.

  :param path:     Filesystem path to the file
  :param info:     Information reported on the file
  :param vfs_name: VFS to work on. Use default VFS if NULL

  :return: EVFS_OK on success



.. c:function:: bool evfs_existing_file_ex(const char *path, const char *vfs_name)

  Test if a file exists.

  :param path:     Filesystem path to the file
  :param vfs_name: VFS to work on. Use default VFS if NULL

  :return: true if the file exists




.. c:function:: bool evfs_existing_dir_ex(const char *path, const char *vfs_name)

  Test if a directory exists.

  :param path:     Filesystem path to the directory
  :param vfs_name: VFS to work on. Use default VFS if NULL

  :return: true if the file exists



.. c:function:: int evfs_delete_ex(const char *path, const char *vfs_name)

  Delete a file or directory.

  :param path:     Filesystem path to the file
  :param vfs_name: VFS to work on. Use default VFS if NULL

  :return: EVFS_OK on success




.. c:function:: int evfs_rename_ex(const char *old_path, const char *new_path, const char *vfs_name)

  Rename a file or directory.

  No validation or transformation is made to the path arguments. Absolute paths
  should match the same parent directory or this is likely to fail.

  :param old_path: Filesystem path to existing file
  :param new_path: New path to the file
  :param vfs_name: VFS to work on. Use default VFS if NULL

  :return: EVFS_OK on success




.. c:function:: int evfs_make_dir_ex(const char *path, const char *vfs_name)

  Make a new directory.

  :param path:     Filesystem path to the directory
  :param vfs_name: VFS to work on. Use default VFS if NULL

  :return: EVFS_OK on success



.. c:function:: int evfs_make_path_ex(const char *path, const char *vfs_name)

  Make a complete path to a nested directory.

  Any missing directories in the path will be created.

  :param path:     Filesystem path to a directory
  :param vfs_name: VFS to work on. Use default VFS if NULL

  :return: EVFS_OK on success



.. c:function:: int evfs_make_path_range_ex(StringRange *path, const char *vfs_name)

  Make a complete path to a nested directory.

  Any missing directories in the path will be created.

  This variant takes a :c:type:`StringRange` object as the path. This allows the directory
  portion of a file path as generated by :c:func:`evfs_path_dirname_ex` to be referenced as
  a substring without making a copy.


  :param path:     Filesystem path to a directory
  :param vfs_name: VFS to work on. Use default VFS if NULL

  :return: EVFS_OK on success




.. c:function:: int evfs_open_dir_ex(const char *path, EvfsDir **dh, const char *vfs_name)

  Open a directory.

  :param path:     Filesystem path to the directory
  :param dh:       Handle for successfully opened directory
  :param vfs_name: VFS to work on. Use default VFS if NULL

  :return: EVFS_OK on success



.. c:function:: int evfs_vfs_open_dir(Evfs *vfs, const char *path, EvfsDir **dh)

  Open a directory from a VFS object.

  :param vfs:      The VFS to open on
  :param path:     Filesystem path to the directory
  :param dh:       Handle for successfully opened directory

  :return: EVFS_OK on success




.. c:function:: int evfs_get_cur_dir_ex(StringRange *cur_dir, const char *vfs_name)

  Get the current working directory for a VFS.

  Note that EVFS does not have any mechanism for handling DOS/Windows-style
  drive volumes. The reported working directory will be for the active volume.

  The returned :c:type:`StringRange` should not be modified as it may point into internal
  EVFS data structures.


  :param cur_dir:  Reference to the current working directory
  :param vfs_name: VFS to work on. Use default VFS if NULL

  :return: EVFS_OK on success



.. c:function:: int evfs_set_cur_dir_ex(const char *path, const char *vfs_name)

  Set the current working directory for a VFS.

  Relative paths will be based in this directory until it is changed.

  :param path:     Path to the new working directory
  :param vfs_name: VFS to work on. Use default VFS if NULL

  :return: EVFS_OK on success



.. c:function:: int evfs_vfs_ctrl_ex(int cmd, void *arg, const char *vfs_name)

  Generic configuration control for a VFS.

  See the definition of commands in evfs.h for the expected type to pass as arg.

  :param cmd:      Command number for the operation to perform
  :param arg:      Variable argument data to write or read associated with the command
  :param vfs_name: VFS to work on. Use default VFS if NULL

  :return: EVFS_OK on success



.. c:function:: int evfs_copy_to_file_ex(const char *dest_path, EvfsFile *fh, char *buf, size_t buf_size, const char *vfs_name)

  Copy contents of an open file to a new file.

  This allows you to transfer files across different VFSs.


  :param dest_path:  Path to the new copy
  :param fh:         Open file to copy from
  :param buf:        Buffer to use for transfers. Use NULL to malloc a temp buffer.
  :param buf_size:   Size of buf array. When buf is NULL this is the size to allocate.
  :param vfs_name:   VFS to work on. Use default VFS if NULL

  :return: EVFS_OK on success





.. _file-methods:

File object methods
-------------------

All of the functions with an "evfs_file" prefix are methods of an :c:type:`EvfsFile` object created by :c:func:`evfs_open_ex`.



.. c:function:: int evfs_file_close(EvfsFile *fh)

  Close a file.

  :param fh:  The file to close

  :return: EVFS_OK on success




.. c:function:: int evfs_file_ctrl(EvfsFile *fh, int cmd, void *arg)

  Generic configuration control for a file object.

  See the definition of commands in evfs.h for the expected type to pass as arg.

  :param fh:  The file to receive the command
  :param cmd: Command number for the operation to perform
  :param arg: Variable argument data to write or read associated with the command

  :return: EVFS_OK on success



.. c:function:: size_t evfs_file_read(EvfsFile *fh, void *buf, size_t size)

  Read data from a file.

  :param fh:   The file to read
  :param buf:  Buffer for read data
  :param size: Size of buf

  :return: Number of bytes read on success or negative error code on failure



.. c:function:: size_t evfs_file_write(EvfsFile *fh, const void *buf, size_t size)

  Write data to a file.

  :param fh:   The file to write
  :param buf:  Buffer for write data
  :param size: Size of buf

  :return: Number of bytes written on success or negative error code on failure



.. c:function:: int evfs_file_truncate(EvfsFile *fh, evfs_off_t size)

  Truncate the length of a file.

  :param fh:   The file to truncate
  :param size: New truncated size

  :return: EVFS_OK on success



.. c:function:: int evfs_file_sync(EvfsFile *fh)

  Sync a file to the underlying filesystem.

  :param fh:   The file to sync

  :return: EVFS_OK on success



.. c:function:: evfs_off_t evfs_file_size(EvfsFile *fh)

  Get the size of a file.

  This will perform a sync to guarantee that intermediate write buffers
  are emptied before checking the size.

  :param fh:   The file to size up

  :return: Size of the open file




.. c:function:: int evfs_file_seek(EvfsFile *fh, evfs_off_t offset, EvfsSeekDir origin)

  Seek to a new offset in a file.

  Origin is one of the following:

  * EVFS_SEEK_TO -  Absolute position from the start of the file
  * EVFS_SEEK_REL -  Position relative to current file offset
  * EVFS_SEEK_REV -  Position from the end of the file

  EVFS_SEEK_REL uses negative values to seek backward and positive
  to go forward. The other origin types use positive offset values.

  Use the :c:macro:`evfs_file_rewind` macro to seek back to offset 0.

  :param fh:   The file to seek on
  :param offset: New position relative to the origin
  :param origin: Start position for the seek

  :return: EVFS_OK on success



.. c:function:: evfs_off_t evfs_file_tell(EvfsFile *fh)

  Get the current position within a file.

  :param fh:   The file to report on

  :return: Current offset into the file from the start



.. c:function:: bool evfs_file_eof(EvfsFile *fh)

  Identify end of file.

  :param fh:   The file to report on

  :return: true if file is at the end






.. _dir-methods:

Directory object methods
------------------------

All of the functions with an "evfs_dir" prefix are methods of an :c:type:`EvfsDir` object created by :c:func:`evfs_open_dir_ex`.


.. c:function:: int evfs_dir_close(EvfsDir *dh)

  Close a directory.

  :param dh:   The directory to close

  :return: EVFS_OK on success



.. c:function:: int evfs_dir_read(EvfsDir *dh, EvfsInfo *info)

  Read the next directory entry.

  Different VFS backends may only support a partial set of :c:type:`EvfsInfo` fields.
  Use the :c:macro:`EVFS_CMD_GET_DIR_FIELDS` command with :c:func:`evfs_vfs_ctrl_ex` to query
  which fields are valid on a particular VFS. Unsupported fields will be 0.


  :param dh:   The directory to read from
  :param info: Information reported on the file

  :return: EVFS_OK on success. EVFS_DONE when iteration is complete.



.. c:function:: int evfs_dir_rewind(EvfsDir *dh)

  Rewind a directory iterator to the beginning.

  :param dh:   The directory to rewind

  :return: EVFS_OK on success




.. c:function:: int evfs_dir_find(EvfsDir *dh, const char *pattern, EvfsInfo *info)

  Find a file matching a glob pattern.

  This scans a directory for a file until a match to pattern is found.
  Repeated calls will find the next file that matches the pattern.

  :param dh:      The directory to search
  :param pattern: Glob pattern to be matched
  :param info:    Information reported on the file

  :return: EVFS_OK on success. EVFS_DONE when iteration is complete.



Miscellaneous
-------------


.. c:function:: const char *evfs_err_name(int err)

  Translate an error code into a string.

  :param err: EVFS error code

  :return: The corresponding string or "<unknown>"




.. c:function:: const char *evfs_cmd_name(int cmd)

  Translate a command code into a string.

  :param cmd: EVFS command code

  :return: The corresponding string or "<unknown>"




.. c:function:: int evfs_file_printf(EvfsFile *fh, const char *fmt, ...)

  Print a formatted string to a file..

  :param fh:   Open file to print into
  :param fmt:  Format string
  :param args: Variable argument list

  :return: Number of bytes written on success or negative error code on failure



.. c:function:: int evfs_file_puts(EvfsFile *fh, const char *str)

  Write a string to a file.

  :param fh:   Open file to print into
  :param str:  String to write

  :return: Number of bytes written on success or negative error code on failure


