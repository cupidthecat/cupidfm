#include "app_options.h"
#include "app_help.h"

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
                !set_option(argument, "--keymap", &options->keymap, &i, argc, argv) &&
                !set_option(argument, "--extract-worker", &options->extract_worker, &i, argc,
                            argv) &&
                !set_option(argument, "--into", &options->extract_into, &i, argc, argv))
                return false;
        } else if (options->entry_count == APP_TAB_LIMIT) {
            return false;
        } else {
            options->entries[options->entry_count++] = argument;
        }
    }
    if (options->extract_worker || options->extract_into)
        return options->extract_worker && options->extract_into && !options->entry_count &&
               !options->cwd_file && !options->chooser_file && !options->keymap && !options->help;
    return !options->keymap || !strcmp(options->keymap, "vim") ||
           !strcmp(options->keymap, "cupidfm");
}

void app_options_usage(FILE *stream, const char *program) {
    fprintf(stream,
            "Usage: %s [OPTIONS] [--] [ENTRY ...]\n"
            "Open up to nine files or directories in tabs; default to the current directory.\n\n"
            "  --keymap=vim|cupidfm  Choose browser shortcuts\n"
            "  --cwd-file=FILE        Write the final directory on quit (Q skips it)\n"
            "  --chooser-file=FILE    Write opened file paths and quit\n"
            "  -h, --help             Show this help\n\n"
            "New configurations use the Vim profile. Existing configurations keep their keys.\n"
            "Options accept --name=VALUE or --name VALUE. Use -- before a path starting with -.\n"
            "Relative output files are resolved from the launch directory.\n"
            "File arguments reveal their names in parent directories; the first tab is active.\n"
            "Chooser output contains one selected path per line.\n\n",
            program);
    fputs(app_help_vim(), stream);
    fputc('\n', stream);
    fputs(app_help_tabs(), stream);
    fputs("\nDefault editor keys: Ctrl+E opens, Ctrl+S saves, Ctrl+Q returns to browsing.\n"
          "Editor bindings and original-profile browser bindings can be set in ~/.cupidfmrc.\n\n",
          stream);
    fputs(app_help_setup(), stream);
    fprintf(stream,
            "\nExamples\n  %s --keymap=vim ~/Documents\n"
            "  %s --cwd-file=/tmp/cupidfm-cwd\n"
            "  %s --chooser-file=/tmp/cupidfm-selection\n"
            "  %s src/app/main.c ~/Documents\n",
            program, program, program, program);
}
