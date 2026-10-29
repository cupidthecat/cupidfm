#define _POSIX_C_SOURCE 200809L
#include "arc_reader.h"
#include "arc_base.h"
#include <errno.h>
#include <fcntl.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static int validate_path(const char *path, const ArcLimits *limits) {
    if (!path || !*path || *path == '/') { errno = EINVAL; return -1; }
    if (strlen(path) > limits->max_name) { errno = EOVERFLOW; return -1; }
    uint64_t depth = 0;
    for (const char *p = path; *p;) {
        const char *end = strchr(p, '/');
        size_t n = end ? (size_t)(end-p) : strlen(p);
        if (n == 2 && p[0] == '.' && p[1] == '.') { errno = EINVAL; return -1; }
        if (n && ++depth > limits->max_nested_depth) { errno = EOVERFLOW; return -1; }
        p += n;
        if (*p) p++;
    }
    return 0;
}

/* Keep each ancestor open. O_NOFOLLOW on a whole path protects only its final
 * component, so every parent must be traversed separately. */
static int open_parent(int root, const char *path, char **leaf, bool create) {
    char *copy = strdup(path);
    if (!copy) return -1;
    int fd = dup(root);
    if (fd < 0) { free(copy); return -1; }
    char *save = NULL;
    char *part = strtok_r(copy, "/", &save);
    if (!part) { close(fd); free(copy); errno = EINVAL; return -1; }
    for (;;) {
        char *next = strtok_r(NULL, "/", &save);
        if (!next) {
            *leaf = strdup(part);
            free(copy);
            if (!*leaf) { close(fd); return -1; }
            return fd;
        }
        if (strcmp(part, ".")) {
            int child = openat(fd, part, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
            if (child < 0 && errno == ENOENT && create) {
                if (mkdirat(fd, part, 0755) && errno != EEXIST) { close(fd); free(copy); return -1; }
                child = openat(fd, part, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
            }
            if (child < 0) { close(fd); free(copy); return -1; }
            close(fd);
            fd = child;
        }
        part = next;
    }
}

static int set_attributes(int fd, const ArcEntry *entry, bool permissions, bool timestamps) {
    if (permissions && fchmod(fd, entry->mode & 0777)) return -1;
    if (timestamps && entry->mtime) {
        struct timespec ts[2] = {{(time_t)entry->mtime, 0}, {(time_t)entry->mtime, 0}};
        if (futimens(fd, ts)) return -1;
    }
    return 0;
}

static void temporary_name(char name[96]) {
    static atomic_ulong sequence;
    struct timespec ts = {0};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    snprintf(name, 96, ".cupidarchive-%ld-%ld-%lu", (long)getpid(), ts.tv_nsec,
             atomic_fetch_add(&sequence, 1));
}

/* Keep zero-filled ranges sparse without changing the logical data stream. */
static int write_contents(int fd,const char *buffer,size_t size) {
    size_t offset=0;
    while (offset<size) {
        size_t count=size-offset>4096 ? 4096 : size-offset;
        bool zero=true;
        for (size_t i=0;i<count;i++) if (buffer[offset+i]) {zero=false;break;}
        if (zero) {
            if (lseek(fd,(off_t)count,SEEK_CUR)<0) return -1;
        } else {
            size_t written=0;
            while (written<count) {
                ssize_t n=write(fd,buffer+offset+written,count-written);
                if (n<0 && errno==EINTR) continue;
                if (n<=0) {if(!n)errno=EIO;return -1;}
                written+=(size_t)n;
            }
        }
        offset+=count;
    }
    return 0;
}

static int extract_file(ArcReader *reader, const ArcEntry *entry, int parent, const char *leaf,
                        bool permissions, bool timestamps) {
    if (!strcmp(leaf, ".")) { errno = EINVAL; return -1; }
    ArcStream *data = arc_open_data(reader);
    if (!data) return -1;
    char temporary[96];
    int fd = -1;
    for (int attempt = 0; attempt < 128; attempt++) {
        temporary_name(temporary);
        fd = openat(parent, temporary, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC,
                    permissions ? entry->mode & 0777 : 0644);
        if (fd >= 0 || errno != EEXIST) break;
    }
    if (fd < 0) { arc_stream_close(data); return -1; }
    int rc = -1;
    uint64_t total = 0;
    const ArcLimits *limits = ((ArcReaderBase *)reader)->limits;
    if (!limits) limits = arc_default_limits();
    char buffer[65536];
    ssize_t n;
    while ((n = arc_stream_read(data, buffer, sizeof(buffer))) > 0) {
        if ((uint64_t)n > limits->max_uncompressed_bytes - total) { errno = EOVERFLOW; goto done; }
        total += (uint64_t)n;
        if (write_contents(fd,buffer,(size_t)n)) goto done;
    }
    if (n < 0) goto done;
    if (((ArcReaderBase *)reader)->format != 2 && total != entry->size) { errno = EIO; goto done; }
    if ((uint64_t)(off_t)total!=total) { errno=EOVERFLOW; goto done; }
    if (ftruncate(fd,(off_t)total) || set_attributes(fd, entry, permissions, timestamps)) goto done;
    if (close(fd)) { fd = -1; goto done; }
    fd = -1;
    if (renameat(parent, temporary, parent, leaf)) goto done;
    rc = 0;
done:;
    int saved = errno;
    if (fd >= 0) close(fd);
    if (rc) unlinkat(parent, temporary, 0);
    arc_stream_close(data);
    errno = saved;
    return rc;
}

static int extract_at(ArcReader *reader, const ArcEntry *entry, int root,
                      bool permissions, bool timestamps) {
    const ArcLimits *limits = ((ArcReaderBase *)reader)->limits;
    if (!limits) limits = arc_default_limits();
    if (validate_path(entry->path, limits)) return -1;
    char *leaf = NULL;
    int parent = open_parent(root, entry->path, &leaf, true);
    if (parent < 0) return -1;
    int rc = -1;
    if (entry->type == ARC_ENTRY_FILE) {
        rc = extract_file(reader, entry, parent, leaf, permissions, timestamps);
    } else if (entry->type == ARC_ENTRY_DIR) {
        if (!mkdirat(parent, leaf, 0755) || errno == EEXIST) {
            int fd = openat(parent, leaf, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
            if (fd >= 0) { rc = set_attributes(fd, entry, permissions, timestamps); close(fd); }
        }
    } else if (entry->type == ARC_ENTRY_SYMLINK || entry->type == ARC_ENTRY_HARDLINK) {
        if (!entry->link_target || !strcmp(leaf, ".")) { errno = EINVAL; goto done; }
        char temporary[96];
        temporary_name(temporary);
        if (entry->type == ARC_ENTRY_SYMLINK) {
            rc = symlinkat(entry->link_target, parent, temporary);
        } else {
            if (validate_path(entry->link_target, limits)) goto done;
            char *target = NULL;
            int source = open_parent(root, entry->link_target, &target, false);
            if (source < 0) goto done;
            struct stat st;
            if (fstatat(source, target, &st, AT_SYMLINK_NOFOLLOW) == 0) {
                if (S_ISREG(st.st_mode)) rc = linkat(source, target, parent, temporary, 0);
                else errno = EINVAL;
            }
            close(source); free(target);
        }
        if (!rc) {
            rc = renameat(parent, temporary, parent, leaf);
            if (rc) unlinkat(parent, temporary, 0);
        }
    } else {
        rc = arc_skip_data(reader);
    }
done:;
    int saved = errno;
    close(parent); free(leaf);
    errno = saved;
    return rc;
}

int arc_extract_entry(ArcReader *reader, const ArcEntry *entry, const char *dest_dir,
                      bool permissions, bool timestamps) {
    if (!reader || !entry || !dest_dir) { errno = EINVAL; return -1; }
    int root = open(dest_dir, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (root < 0) return -1;
    int rc = extract_at(reader, entry, root, permissions, timestamps);
    int saved = errno;
    close(root); errno = saved;
    return rc;
}

typedef struct Pending {
    ArcEntry entry;
    struct Pending *next;
    size_t order;
} Pending;

static void canonical_directory(char *path) {
    char *read=path,*write=path;
    while (*read) {
        while (*read=='/') read++;
        if (!*read) break;
        char *end=strchr(read,'/');
        size_t size=end ? (size_t)(end-read) : strlen(read);
        if (size && !(size==1 && *read=='.')) {
            memmove(write,read,size);write+=size;*write++='/';
        }
        if (!end) break;
        read=end+1;
    }
    if (write!=path) write[-1]=0;
    else {path[0]='.';path[1]=0;}
}
static size_t directory_depth(const char *path) {
    if (!strcmp(path,".")) return 0;
    size_t depth=1;
    for (;*path;path++) if (*path=='/') depth++;
    return depth;
}
static int directory_order(const void *left,const void *right) {
    const Pending *a=*(const Pending *const *)left,*b=*(const Pending *const *)right;
    size_t ad=directory_depth(a->entry.path),bd=directory_depth(b->entry.path);
    if (ad!=bd) return ad>bd ? -1 : 1;
    int order=strcmp(a->entry.path,b->entry.path);
    if (order) return order;
    return a->order==b->order ? 0 : a->order>b->order ? -1 : 1;
}

int arc_extract_to_path(ArcReader *reader, const char *dest_dir, bool permissions, bool timestamps) {
    if (!reader || !dest_dir) { errno = EINVAL; return -1; }
    int root = open(dest_dir, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (root < 0) return -1;
    int rc = 0, saved = 0, next;
    Pending *links = NULL, *directories = NULL;
    size_t directory_count = 0;
    ArcEntry entry;
    while ((next = arc_next(reader, &entry)) == 0) {
        /* Directory metadata is applied after children, in reverse archive order. */
        bool directory = entry.type == ARC_ENTRY_DIR;
        int result = extract_at(reader, &entry, root, directory ? false : permissions,
                                directory ? false : timestamps);
        bool pending_link = result < 0 && entry.type == ARC_ENTRY_HARDLINK && errno == ENOENT;
        if ((!result && directory) || pending_link) {
            Pending *item = malloc(sizeof(*item));
            if (!item) { arc_entry_free(&entry); rc = -1; saved = ENOMEM; break; }
            item->entry = entry;
            item->order = directory ? directory_count++ : 0;
            if (directory) canonical_directory(item->entry.path);
            Pending **head = directory ? &directories : &links;
            item->next = *head; *head = item;
        } else {
            if (result < 0) { rc = -1; saved = errno; }
            arc_entry_free(&entry);
        }
    }
    if (next < 0) { rc = -1; saved = errno; }
    while (links) {
        bool progress = false;
        Pending **item = &links;
        while (*item) {
            Pending *current = *item;
            if (!extract_at(reader, &current->entry, root, permissions, timestamps)) {
                *item = current->next;
                arc_entry_free(&current->entry); free(current); progress = true;
            } else item = &current->next;
        }
        if (!progress) { rc = -1; saved = ENOENT; break; }
    }
    while (links) {
        Pending *item = links; links = item->next;
        arc_entry_free(&item->entry); free(item);
    }
    if (directory_count) {
        Pending **ordered = directory_count <= SIZE_MAX / sizeof(*ordered) ? malloc(directory_count * sizeof(*ordered)) : NULL;
        if (!ordered) { rc = -1; saved = ENOMEM; }
        else {
            size_t i = 0;
            for (Pending *item = directories; item; item = item->next) ordered[i++] = item;
            qsort(ordered,directory_count,sizeof(*ordered),directory_order);
            for (i = 0; i < directory_count; i++) {
                if (i && !strcmp(ordered[i-1]->entry.path,ordered[i]->entry.path)) continue;
                if (extract_at(reader,&ordered[i]->entry,root,permissions,timestamps)) { rc = -1; saved = errno; }
            }
            free(ordered);
        }
    }
    while (directories) {
        Pending *item = directories; directories = item->next;
        arc_entry_free(&item->entry); free(item);
    }
    close(root); errno = saved;
    return rc;
}
