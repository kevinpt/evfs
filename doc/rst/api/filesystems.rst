===========
Filesystems
===========

EVFS supports multiple filesystem interface backends. In addition to system level file access via C stdio and POSIX calls, the library can access
FatFs and littlefs filesystems either directly from their storage media or mounted as images within another EVFS filesystem. Each filesystem has a registration function that wraps :c:func:`evfs_register`, adding the necessary arguments for configuration and constructing dynamic structures.


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


