#ifndef VIRTUALFS_H
#define VIRTUALFS_H

#include <stdbool.h>
#include <sys/statfs.h>

// Include Linux-specific magic numbers when available
#ifdef __linux__
#include <linux/magic.h>
#endif

/* Returns true if the statfs type represents a pseudo / memory fs. */
static inline bool is_virtual_fstype(long t) {
    (void)t;  // Suppress unused parameter warning when no FS types are defined
    
    /* Values are from <linux/magic.h>. Wrap each one with #ifdef so the
       code still builds on non-Linux systems that do not define them. */
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
    if (t == FUSE_SUPER_MAGIC)      return true;  // FUSE filesystems
#endif
    return false;
}

#endif // VIRTUALFS_H 