#include "app_options.h"

#include <string.h>

static bool set_option(const char *argument, const char *name, const char **field, int *index,
                       int argc, char **argv) {
    size_t length = strlen(name);
    if (strncmp(argument, name, length))
        return false;
    if (argument[length] == '=') {
        *field = argument + length + 1;
        return **field != '\0';
    }
    if (!argument[length] && *index + 1 < argc) {
        *field = argv[++*index];
        return **field != '\0';
    }
    return false;
}

bool app_options_parse(int argc, char **argv, AppOptions *options) {
    *options = (AppOptions){0};
    bool positional = false;
    for (int i = 1; i < argc; i++) {
        const char *argument = argv[i];
        if (!positional && !strcmp(argument, "--")) {
            positional = true;
        } else if (!positional && (!strcmp(argument, "--help") || !strcmp(argument, "-h"))) {
            options->help = true;
        } else if (!positional && argument[0] == '-') {
            if (!set_option(argument, "--cwd-file", &options->cwd_file, &i, argc, argv) &&
                !set_option(argument, "--chooser-file", &options->chooser_file, &i, argc, argv) &&
                !set_option(argument, "--keymap", &options->keymap, &i, argc, argv))
                return false;
        } else if (options->directory) {
            return false;
        } else {
            options->directory = argument;
        }
    }
    return !options->keymap || !strcmp(options->keymap, "vim") ||
           !strcmp(options->keymap, "cupidfm");
}

void app_options_usage(FILE *stream, const char *program) {
    fprintf(stream,
            "Usage: %s [OPTIONS] [--] [DIRECTORY]\n"
            "Open DIRECTORY, or the current directory when omitted.\n\n"
            "  --keymap=vim|cupidfm  Choose browser shortcuts\n"
            "  --cwd-file=FILE        Write the final directory on quit (Q skips it)\n"
            "  --chooser-file=FILE    Write opened file paths and quit\n"
            "  -h, --help             Show this help\n",
            program);
}
