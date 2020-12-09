#ifndef SHIM_JAIL_H
#define SHIM_JAIL_H

int evfs_register_jail(const char *vfs_name, const char *old_vfs_name, const char *jail_root, bool default_vfs);

#endif // SHIM_JAIL_H
