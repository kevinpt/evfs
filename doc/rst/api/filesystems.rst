===========
Filesystems
===========

EVFS supports multiple filesystem interface backends. In addition to system level file access via C stdio and POSIX calls, the library can access
FatFs and littlefs filesystems either directly from their storage media or mounted as images within another EVFS filesystem. There are also two filesystems that can mount an archive in tar format stored on an existing VFS or as statically linked data. The Linux Romfs image format can be used as a more compact read-only filesystem. Each filesystem has a registration function that wraps :c:func:`evfs_register`, adding the necessary arguments for configuration and constructing dynamic structures.


Stdio
-----

EVFS implements a special "stdio" filesystem which uses the C file access API. Essentially everything that works with a :c:texpr:`FILE *`. Directory operations are not supported by the C standard library so POSIX API calls are used for those features. If your target system does not support POSIX you can undefine :c:macro:`EVFS_USE_STDIO_POSIX` in 'evfs_config.h' and work with limited functionality. There is only one "stdio" filesystem per process so you just need to register this once for all common file I/O.


.. c:function:: int evfs_register_stdio(bool default_vfs)

  Register a stdio instance.

  This VFS is always named "stdio". There should only be one instance per application.

  :param default_vfs:   Make this the default VFS when true

  :return: EVFS_OK on success




.. _fatfs-fs:

FatFs
-----

To access a `FatFs <http://elm-chan.org/fsw/ff/00index_e.html>`_ filesystem you need to call :c:func:`evfs_register_fatfs` with the FatFs volume number. Because FatFs uses global state to track its
filesystems you need to handle configuring storage access through its 'disk_*()' callback functions. The callbacks don't need to be functioning before registration.


.. c:function:: int evfs_register_fatfs(const char *vfs_name, uint8_t pdrv, bool default_vfs)

  Register a new FatFs instance.

  :param vfs_name:    Name of new VFS
  :param pdrv:        FatFs volume number
  :param default_vfs: Make this the default VFS when true

  :return: EVFS_OK on success


A FatFs filesystem stored in an image file can be mounted using the helper functions in 'fatfs_image.c'. The following functions will let you mount an image. The provided FatFs callback methods will need to be customized if additional volumes need to be mounted at the same time.


.. c:function:: int fatfs_make_image(const char *img_path, uint8_t pdrv, evfs_off_t img_size)

  Make a FatFs image file if it doesn't exist.

  If a new image is created it will be formatted.

  :param img_path:  Path to the image file
  :param pdrv:      Any unused FatFs volume number
  :param img_size:  Size of the image to generate


  :return: EVFS_OK on success



.. c:function:: int fatfs_mount_image(const char *img_path, uint8_t pdrv)

  Mount a FatFs image.


  :param img_path:  Path to the image file
  :param pdrv:      FatFs volume number where this image will be mounted

  :return: EVFS_OK on success



.. c:function:: void fatfs_unmount_image(uint8_t pdrv)

  Unmount a FatFs image.

  :param pdrv:      FatFs volume number where this image is mounted


If you need a new image, one can be prepared using :c:func:`fatfs_make_image`. This will not alter an existing image file. When the image is ready you mount it within FatFs using :c:func:`fatfs_mount_image`. At this point the image is associated with a FatFs volume number. You can then register a FatFs instance using that volume number. The contents of the image can then be read and modified as usual using the EVFS API. When done with the image you should unmount it within FatFs using :c:func:`fatfs_unmount_image`.

.. literalinclude:: ex_fatfs_image.c
  :language: c
  :caption: Using a FatFs image file



.. _littlefs-fs:

littlefs
--------

Use :c:func:`evfs_register_littlefs` to access a `littlefs <https://github.com/littlefs-project/littlefs>`_ filesystem. Littlefs keeps track of filesystem state through an :c:type:`lfs_t` struct. It is easy to attach multiple littlefs volumes with their own EVFS filesystem name. Each one can be registered as an independent VFS.


.. c:function:: int evfs_register_littlefs(const char *vfs_name, lfs_t *lfs, bool default_vfs)

  Register a Littlefs instance

  :param vfs_name:      Name of new VFS
  :param lfs:           Mounted littlefs object
  :param default_vfs:   Make this the default VFS when true

  :return: EVFS_OK on success



A littlefs filesystem stored in an image file can be mounted using the helper functions in 'littlefs_image.c'. The following functions will let you mount an image. The provided littlefs callback methods can be used as is. Wear leveling has no useful purpose in an image file stored on another filesystem. The :c:type:`lfs_config` struct should have its :c:var:`block_cycles` member set to -1 to disable this feature.


.. c:function:: int littlefs_make_image(const char *img_path, struct lfs_config *cfg)

  Make a Littlefs image file if it doesn't exist.

  Image geometry is taken from the configuration data.


  :param img_path:  Path to the image file
  :param cfg:       Configuration struct for the Littlefs this is mounted on

  :return: EVFS_OK on success


.. c:function:: int littlefs_mount_image(const char *img_path, struct lfs_config *cfg, lfs_t *lfs)

  Mount a Littlefs image


  :param img_path:  Path to the image file
  :param cfg:       Configuration struct for the Littlefs this is mounted on
  :param lfs:       Littlefs filesystem object to mount onto

  :return: EVFS_OK on success



.. c:function:: void littlefs_unmount_image(lfs_t *lfs)

  Unmount a Littlefs image.

  :param lfs:       Mounted Littlefs filesystem object



Image handling functions are similar to those for FatFs. All callback functions are supplied in the :c:type:`lfs_config` struct. 



.. literalinclude:: ex_littlefs_image.c
  :language: c
  :caption: Using a littlefs image file


.. _tar-fs:

Tar FS
------

Use :c:func:`evfs_register_tar_fs` to mount an uncompressed tar file as a virtual filesystem. Data in the tar file is read only. Only normal files are supported. You cannot open a directory object to get a file listing.

You will need to have an existing VFS registered to open the tar file needed by the registration function. When generating a tar file you should reduce the blocking factor to 1 to minimize wasted padding after each file. This will impose an overhead of 512 bytes for each file header plus an average 256 bytes of padding after each file (assuming random file sizes).

.. c:function:: int evfs_register_tar_fs(const char *vfs_name, EvfsFile *tar_file, bool default_vfs)

  Register a Tar FS instance

  :param vfs_name:      Name of new VFS
  :param tar_file:      EVFS file of tar data
  :param default_vfs:   Make this the default VFS when true

  :return: EVFS_OK on success


.. _tar-rsrc-fs:

Tar resource FS
---------------

This is an alternative to the Tar FS that lets you use in memory data resources encoded in tar format in place of a normal filesystem. Use :c:func:`evfs_register_tar_rsrc_fs` to mount a static array in tar format as a virtual filesystem. 

Data in the tar resource is read only. Only normal files are supported. You cannot open a directory object to get a file listing.

The tar resource data should be statically linked into your program. This can be generated by using "xxd -i" to convert a tar file into a C header. You can also create an assembly wrapper that uses the
``.incbin`` directive or you can configure a linker script to link a tar file directly into its own section.

.. code-block:: sh

  > tar cfb foo.tar 1 src
  > xxd -i foo.tar > foo_tar.h

.. code-block:: c

  #include "evfs.h"
  #include "evfs/tar_rsrc_fs.h"

  #include "foo_tar.h"

  // Mount the tar resource from array in foo_tar.h
  // xxd generates an array and length variable in the header
  evfs_register_tar_rsrc_fs("tarfs", foo_tar, foo_tar_len, /*default*/ true);

  EvfsFile fh;
  evfs_open("bar.txt", &fh, EVFS_READ);
  ...
  evfs_file_close(fh);


File data can be accessed using the usual EVFS API with :c:func:`evfs_file_read`. You can also directly access the resource data for a file by passing the `EVFS_CMD_GET_RSRC_ADDR` command to :c:func:`evfs_file_ctrl` and using :c:func:`evfs_file_size` for the in-memory array bounds.

.. code-block:: c

  EvfsFile fh;
  evfs_open("bar.txt", &fh, EVFS_READ);

  // Get direct pointer to resource data for "bar.txt"
  uint8_t *bar_txt_data;
  evfs_file_ctrl(fh, EVFS_CMD_GET_RSRC_ADDR, &bar_txt_data);
  size_t bar_txt_len = evfs_file_size(fh);


.. c:function:: int evfs_register_tar_rsrc_fs(const char *vfs_name, uint8_t *resource, size_t resource_len, bool default_vfs)

  Register a Tar resource FS instance

  :param vfs_name:      Name of new VFS
  :param resource:      Array of Tar resource data
  :param resource_len:  Length of the resource array
  :param default_vfs:   Make this the default VFS when true

  :return: EVFS_OK on success


.. _romfs:

Romfs
-----

The Romfs driver provides the ability to mount Linux Romfs images. This is a simple read-only filesystem that has less overhead than the tar file filesystem and has a fully navigable directory structure. The images can either be accessed as files via an existing VFS or as an in-memory array of data.

The :c:func:`evfs_register_romfs` function is used to mount a Romfs image from a VFS. You need to pass it a pointer to an opened image file containing the filesystem data. Romfs images are created with the `genromfs <https://github.com/chexum/genromfs>`_ program which is available in most Linux distros.

The :c:func:`evfs_register_rsrc_romfs` function is used to mount an in-memory array as a Romfs. You will need to link a Romfs image into the program similar to the methods described for the tar resource fs.

There is a configuration option :c:macro:`EVFS_USE_ROMFS_FAST_INDEX` that lets you control the generation of a hash table index. When enabled, path lookups are O(1) through a hash table. When disabled, files are found by walking the directory tree.

Unlike the tar fs implementation, the Romfs driver supports directory operations. You can create an :c:type:`EvfsDir` object and list directory contents like any other filesystem.

.. code-block:: sh

  > genromfs -d image_dir -f my_image.romfs -V MyImage -v
 
.. code-block:: c

  #include "evfs.h"
  #include "evfs/romfs_fs.h"

  evfs_register_stdio(/*default*/ true);
  EvfsFile *image;

  evfs_open("my_image.romfs", &image, EVFS_READ); // Open image on stdio

  // Mount image and make it default for future VFS access
  evfs_register_romfs("romfs", image, /*default*/ true);

  EvfsFile fh;
  evfs_open("foo.txt", &fh, EVFS_READ); // Open file on Romfs image
  ...
  evfs_file_close(fh);

  // Image file is closed when VFS is unregistered



Mounting a Romfs resource is similar:

.. code-block:: sh

  > genromfs -d image_dir -f my_image.romfs -V MyImage -v
  > xxd -i my_image.romfs > my_image.h
 
.. code-block:: c

  #include "evfs.h"
  #include "evfs/romfs_fs.h"

  #include "my_image.h"

  // Mount resource and make it default for future VFS access
  evfs_register_rsrc_romfs("romfs", my_image_romfs, my_image_romfs_len, /*default*/ true);

Converting a large filesystem image into a header generates an unnecessary amount of text for the compiler to process. To avoid this you can generate an assembly stub that uses the ``.incbin`` directive for direct inclusion of the image data:

.. code-block:: gas

    .section .rodata

  # Include filesystem image
    .global my_image
    .type my_image, @object
    .align 4
  my_image:
    .incbin "my_image.romfs"
  my_image_end:
    .byte 0

  # Compute size of the image
    .global my_image_size
    .type my_image_size, @object
    .align 4
  my_image_size:
    .4byte  my_image_end - my_image

This can be saved as `my_image.s` and linked with a compiler that suports gcc assembly directives. The labels "my_image" and "my_image_size" are referenced from C code when registering the filesystem:

.. code-block:: c

  extern const uint8_t   my_image[];
  extern const uint32_t  my_image_size;
  ...
  evfs_register_rsrc_romfs("romfs", my_image, my_image_size, /*default*/ true);


You can pass the `EVFS_CMD_GET_RSRC_ADDR` command to :c:func:`evfs_file_ctrl` to directly access in-memory resource data as shown above for the tar resource FS.


.. c:function:: int evfs_register_romfs(const char *vfs_name, EvfsFile *image, bool default_vfs)

  Register a Romfs instance using an image file

  :param vfs_name:      Name of new VFS
  :param image:         Mounted Romfs image
  :param default_vfs:   Make this the default VFS when true

  :return: EVFS_OK on success

.. c:function:: int evfs_register_rsrc_romfs(const char *vfs_name, uint8_t *resource, size_t resource_len, bool default_vfs)

  Register a Romfs instance using an in-memory resource array

  :param vfs_name:      Name of new VFS
  :param resource:      Array of Romfs resource data
  :param resource_len:  Length of the resource array
  :param default_vfs:   Make this the default VFS when true

  :return: EVFS_OK on success


