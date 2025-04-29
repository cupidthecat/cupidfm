// src/virtualfs.h
#ifndef VIRTUALFS_H
#define VIRTUALFS_H

#include <stdbool.h>
#include <sys/statfs.h>

#ifdef __linux__
#  include <linux/magic.h>
#endif

/**
 * Returns true if the statfs type represents a pseudo / memory fs.
 */
static inline bool is_virtual_fstype(long t) {
#ifdef PROC_SUPER_MAGIC
    if (t == PROC_SUPER_MAGIC)      return true;  // procfs
#endif
#ifdef SYSFS_MAGIC
    if (t == SYSFS_MAGIC)           return true;  // sysfs
#endif
#ifdef DEVPTS_SUPER_MAGIC
    if (t == DEVPTS_SUPER_MAGIC)    return true;  // devpts
#endif
#ifdef TMPFS_MAGIC
    if (t == TMPFS_MAGIC)           return true;  // tmpfs
#endif
#ifdef CGROUP_SUPER_MAGIC
    if (t == CGROUP_SUPER_MAGIC)    return true;  // cgroup
#endif
#ifdef CGROUP2_SUPER_MAGIC
    if (t == CGROUP2_SUPER_MAGIC)   return true;  // cgroup2
#endif
#ifdef OVERLAYFS_SUPER_MAGIC
    if (t == OVERLAYFS_SUPER_MAGIC) return true;  // overlayfs
#endif
#ifdef FUSE_SUPER_MAGIC
    if (t == FUSE_SUPER_MAGIC)      return true;  // fuse
#endif
#ifdef SQUASHFS_MAGIC
    if (t == SQUASHFS_MAGIC)        return true;  // squashfs (e.g. /snap)
#endif
#ifdef SECURITYFS_MAGIC
    if (t == SECURITYFS_MAGIC)      return true;  // securityfs
#endif
#ifdef DEBUGFS_MAGIC
    if (t == DEBUGFS_MAGIC)         return true;  // debugfs
#endif
#ifdef TRACEFS_MAGIC
    if (t == TRACEFS_MAGIC)         return true;  // tracefs
#endif
#ifdef CONFIGFS_MAGIC
    if (t == CONFIGFS_MAGIC)        return true;  // configfs
#endif
#ifdef MQUEUE_MAGIC
    if (t == MQUEUE_MAGIC)          return true;  // POSIX mqueue
#endif
#ifdef AUTOFS_SUPER_MAGIC
    if (t == AUTOFS_SUPER_MAGIC)    return true;  // autofs
#endif
#ifdef BPF_FS_MAGIC
    if (t == BPF_FS_MAGIC)          return true;  // bpffs
#endif
#ifdef EFIVARFS_MAGIC
    if (t == EFIVARFS_MAGIC)        return true;  // efivarfs
#endif

    return false;
}

#endif // VIRTUALFS_H
