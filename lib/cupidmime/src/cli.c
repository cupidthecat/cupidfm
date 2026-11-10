#include "cupidmime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(FILE *f) {
    fputs("Usage: cupidmime [-v] [--] FILE...\n"
          "Detect content types; use - for stdin. -v prints evidence and flags.\n",
          f);
}

int main(int argc, char **argv) {
    unsigned char *buffer;
    int i, verbose = 0, options = 1, count = 0, failed = 0;
    buffer = malloc(CM_SCAN_LIMIT + 1);
    if (!buffer) {
        fputs("cupidmime: out of memory\n", stderr);
        return 1;
    }
    for (i = 1; i < argc; ++i) {
        FILE *f;
        size_t n;
        cm_result r;
        if (options && !strcmp(argv[i], "--")) {
            options = 0;
            continue;
        }
        if (options && !strcmp(argv[i], "-v")) {
            verbose = 1;
            continue;
        }
        if (options && !strcmp(argv[i], "--help")) {
            usage(stdout);
            free(buffer);
            return 0;
        }
        if (options && !strcmp(argv[i], "--version")) {
            puts(cm_version());
            free(buffer);
            return 0;
        }
        if (options && argv[i][0] == '-' && argv[i][1]) {
            fprintf(stderr, "cupidmime: unknown option: %s\n", argv[i]);
            free(buffer);
            return 2;
        }
        ++count;
        f = !strcmp(argv[i], "-") ? stdin : fopen(argv[i], "rb");
        if (!f) {
            perror(argv[i]);
            failed = 1;
            continue;
        }
        n = fread(buffer, 1, CM_SCAN_LIMIT + 1, f);
        if (ferror(f)) {
            fprintf(stderr, "cupidmime: cannot read %s\n", argv[i]);
            failed = 1;
        } else {
            (void)cm_detect(buffer, n, &r);
            printf("%s: %s", argv[i], r.mime);
            if (verbose)
                printf(" [evidence=%u flags=%u examined=%zu] %s", (unsigned)r.evidence, r.flags,
                       r.examined, r.reason);
            putchar('\n');
        }
        if (f != stdin && fclose(f)) {
            perror(argv[i]);
            failed = 1;
        }
    }
    free(buffer);
    if (!count) {
        usage(stderr);
        return 2;
    }
    return failed ? 1 : 0;
}
