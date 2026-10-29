#define _POSIX_C_SOURCE 200809L
#include "cupidgit.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(int argc, char **argv) {
    if (argc != 2) return 2;
    GitRepository *repo = git_repo_open(argv[1]);
    puts(repo ? "open" : "error");
    fflush(stdout);
    char *line = NULL;
    size_t cap = 0;
    ssize_t n;
    while ((n = getline(&line, &cap, stdin)) >= 0) {
        if (n && line[n-1] == '\n') line[n-1] = 0;
        printf("%d\n", git_file_status(repo, line));
        fflush(stdout);
    }
    free(line);
    git_repo_free(repo);
}
