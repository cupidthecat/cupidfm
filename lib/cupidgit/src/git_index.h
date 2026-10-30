#ifndef GIT_INDEX_H
#define GIT_INDEX_H

#include "git_repo.h"
#include <stdint.h>
#include <time.h>

#define GIT_SHA1_SIZE 20

// Index entry structure
typedef struct {
    time_t mtime_sec;
    uint32_t mtime_nsec;
    uint32_t mode;
    uint16_t flags;
    uint16_t extended_flags;
    unsigned char sha1[GIT_SHA1_SIZE];
    char *path; // Relative to repo root
} GitIndexEntry;

// Simple hash map for index entries
typedef struct {
    GitIndexEntry *entries;
    size_t count;
    size_t capacity;
} IndexMap;

// Parse .git/index file and populate repository
bool git_index_parse(GitRepository *repo);

bool git_index_refresh(GitRepository *repo);

// Free index map
void git_index_free(IndexMap *map);

// Lookup entry by path
GitIndexEntry *git_index_lookup(IndexMap *map, const char *path);

#endif // GIT_INDEX_H
