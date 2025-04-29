#ifndef VIRTUALFS_H
#define VIRTUALFS_H

#include <stdbool.h>
#include <sys/statfs.h>

/* Returns true if the statfs type represents a pseudo / memory fs. */
static inline bool is_virtual_fstype(long t) {
    (void)t;  // Suppress unused parameter warning when no FS types are defined
    
    /* Values are from <linux/magic.h>. Wrap each one with #ifdef so the
       code still builds on non-Linux systems that do not define them. */
#ifdef PROC_SUPER_MAGIC
    if (t == PROC_SUPER_MAGIC)      return true;
#endif
#ifdef SYSFS_MAGIC
    if (t == SYSFS_MAGIC)           return true;
#endif
#ifdef DEVPTS_SUPER_MAGIC
    if (t == DEVPTS_SUPER_MAGIC)    return true;
#endif
#ifdef DEVFS_SUPER_MAGIC
    if (t == DEVFS_SUPER_MAGIC)     return true;
#endif
#ifdef TMPFS_MAGIC
    if (t == TMPFS_MAGIC)           return true;
#endif
#ifdef CGROUP_SUPER_MAGIC
    if (t == CGROUP_SUPER_MAGIC)    return true;
#endif
#ifdef CGROUP2_SUPER_MAGIC
    if (t == CGROUP2_SUPER_MAGIC)   return true;
#endif
#ifdef MQUEUE_MAGIC
    if (t == MQUEUE_MAGIC)          return true;
#endif
#ifdef DEBUGFS_MAGIC
    if (t == DEBUGFS_MAGIC)         return true;
#endif
#ifdef TRACEFS_MAGIC
    if (t == TRACEFS_MAGIC)         return true;
#endif
#ifdef OVERLAYFS_SUPER_MAGIC
    if (t == OVERLAYFS_SUPER_MAGIC) return true;
#endif
#ifdef FUSE_SUPER_MAGIC
    if (t == FUSE_SUPER_MAGIC)      return true;
#endif
    return false;          /* treat everything else as "real" storage */
}

#endif // VIRTUALFS_H 
