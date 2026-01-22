#include <stdio.h>
#include <string.h>
#define MAX_PATH_LENGTH 1024

void path_join_mut2(char *result, const char *base, const char *extra) {
    size_t base_len = strlen(base);
    size_t extra_len = strlen(extra);
    if (base_len == 0) {
        strncpy(result, extra, MAX_PATH_LENGTH);
    } else if (extra_len == 0) {
        strncpy(result, base, MAX_PATH_LENGTH);
    } else {
        if (base[base_len - 1] == '/') {
            snprintf(result, MAX_PATH_LENGTH, "%s%s", base, extra);
        } else {
            snprintf(result, MAX_PATH_LENGTH, "%s\\%s", base, extra);
        }
    }
    result[MAX_PATH_LENGTH - 1] = '\0';
}

int main() {
    char result[1024];
    path_join_mut2(result, "/home", "user");
    printf("Result: '%s'\n", result);
    printf("Expected: '/home/user'\n");
    printf("Match: %d\n", strcmp(result, "/home/user") == 0);
    return 0;
}
