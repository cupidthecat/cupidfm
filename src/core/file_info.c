#define _GNU_SOURCE
#include "file_info.h"

#include <errno.h>
#include <fcntl.h>
#include <glib.h>
#include <grp.h>
#include <pwd.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "mime.h"

static void add(FileInfo *info, const char *label, const char *format, ...) {
    if (info->count == G_N_ELEMENTS(info->fields))
        return;
    va_list arguments;
    va_start(arguments, format);
    info->fields[info->count++] = (FileInfoField){label, g_strdup_vprintf(format, arguments)};
    va_end(arguments);
}

static void timestamp(FileInfo *info, const char *label, time_t value) {
    struct tm local;
    char text[80];
    if (localtime_r(&value, &local) && strftime(text, sizeof(text), "%Y-%m-%d %H:%M:%S %z", &local))
        add(info, label, "%s", text);
    else
        add(info, label, "-");
}

void file_info_clear(FileInfo *info) {
    if (!info)
        return;
    for (size_t i = 0; i < info->count; i++)
        g_free(info->fields[i].value);
    *info = (FileInfo){0};
}

void file_info_read(FileInfo *info, const char *path, magic_t cookie) {
    if (!info)
        return;
    file_info_clear(info);
    if (!path)
        return;
    char *name = g_path_get_basename(path);
    add(info, "Name", "%s", name);
    g_free(name);
    add(info, "Path", "%s", path);
    struct stat metadata;
    if (lstat(path, &metadata)) {
        add(info, "Error", "%s", g_strerror(errno));
        return;
    }
    const char *type = S_ISREG(metadata.st_mode)    ? "Regular file"
                       : S_ISDIR(metadata.st_mode)  ? "Directory"
                       : S_ISLNK(metadata.st_mode)  ? "Symbolic link"
                       : S_ISFIFO(metadata.st_mode) ? "FIFO"
                       : S_ISSOCK(metadata.st_mode) ? "Socket"
                       : S_ISCHR(metadata.st_mode)  ? "Character device"
                       : S_ISBLK(metadata.st_mode)  ? "Block device"
                                                    : "Other";
    add(info, "Type", "%s", type);
    if (S_ISREG(metadata.st_mode)) {
        char buffer[512];
        const char *mime = mime_type_cached(cookie, path, MAGIC_MIME_TYPE, buffer, sizeof(buffer));
        add(info, "MIME", "%s", mime ? mime : "Unknown");
    }
    guint64 size = metadata.st_size < 0 ? 0 : (guint64)metadata.st_size;
    char *human = g_format_size_full(size, G_FORMAT_SIZE_IEC_UNITS);
    add(info, S_ISDIR(metadata.st_mode) ? "Entry size" : "Size", "%llu bytes (%s)",
        (unsigned long long)size, human);
    g_free(human);
    char permissions[10] = "---------";
    const char *bits = "rwxrwxrwx";
    for (int i = 0; i < 9; i++)
        if (metadata.st_mode & (0400 >> i))
            permissions[i] = bits[i];
    if (metadata.st_mode & S_ISUID)
        permissions[2] = metadata.st_mode & S_IXUSR ? 's' : 'S';
    if (metadata.st_mode & S_ISGID)
        permissions[5] = metadata.st_mode & S_IXGRP ? 's' : 'S';
    if (metadata.st_mode & S_ISVTX)
        permissions[8] = metadata.st_mode & S_IXOTH ? 't' : 'T';
    add(info, "Permissions", "%s (%04o)", permissions, (unsigned)(metadata.st_mode & 07777));
    struct passwd owner, *owner_result = NULL;
    struct group group, *group_result = NULL;
    char owner_buffer[4096], group_buffer[4096];
    if (!getpwuid_r(metadata.st_uid, &owner, owner_buffer, sizeof(owner_buffer), &owner_result) &&
        owner_result)
        add(info, "Owner", "%s (%lu)", owner.pw_name, (unsigned long)metadata.st_uid);
    else
        add(info, "Owner", "%lu", (unsigned long)metadata.st_uid);
    if (!getgrgid_r(metadata.st_gid, &group, group_buffer, sizeof(group_buffer), &group_result) &&
        group_result)
        add(info, "Group", "%s (%lu)", group.gr_name, (unsigned long)metadata.st_gid);
    else
        add(info, "Group", "%lu", (unsigned long)metadata.st_gid);
    timestamp(info, "Modified", metadata.st_mtime);
    timestamp(info, "Accessed", metadata.st_atime);
    timestamp(info, "Changed", metadata.st_ctime);
#if defined(__linux__) && defined(STATX_BTIME)
    struct statx extended;
    if (!statx(AT_FDCWD, path, AT_SYMLINK_NOFOLLOW, STATX_BTIME, &extended) &&
        (extended.stx_mask & STATX_BTIME))
        timestamp(info, "Created", (time_t)extended.stx_btime.tv_sec);
    else
#endif
        add(info, "Created", "-");
    add(info, "Inode", "%llu", (unsigned long long)metadata.st_ino);
    add(info, "Links", "%llu", (unsigned long long)metadata.st_nlink);
    if (S_ISLNK(metadata.st_mode)) {
        GError *error = NULL;
        char *target = g_file_read_link(path, &error);
        add(info, "Target", "%s", target ? target : error ? error->message : "Unreadable link");
        g_clear_error(&error);
        g_free(target);
    }
}
