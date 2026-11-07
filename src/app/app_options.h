#ifndef APP_OPTIONS_H
#define APP_OPTIONS_H

#include <stdbool.h>
#include <stdio.h>

typedef struct {
    const char *directory;
    const char *cwd_file;
    const char *chooser_file;
    const char *keymap;
    bool help;
} AppOptions;

bool app_options_parse(int argc, char **argv, AppOptions *options);
void app_options_usage(FILE *stream, const char *program);

#endif
