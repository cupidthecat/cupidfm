#define _POSIX_C_SOURCE 200809L
#include "cupidarchive.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(int argc, char **argv) {
    if (argc < 4) return 2;
    void *buffer = NULL;
    ArcReader *reader;
    if (!strcmp(argv[1], "stream")) {
        FILE *fp = fopen(argv[2], "rb");
        if (!fp || fseek(fp,0,SEEK_END)) return 2;
        long size = ftell(fp);
        if (size < 0 || fseek(fp,0,SEEK_SET)) return 2;
        buffer = malloc(size ? (size_t)size : 1);
        if (!buffer || fread(buffer,1,(size_t)size,fp)!=(size_t)size) return 2;
        fclose(fp);
        ArcStream *stream = arc_stream_from_memory(buffer,(size_t)size,0);
        reader = arc_open_stream(stream);
        if (!reader) arc_stream_close(stream);
    } else if (argc == 5) {
        ArcLimits limits = {.max_entries = 1};
        reader = arc_open_path_ex(argv[2], &limits);
        limits.max_entries = 1000;
        ArcReader *other = arc_open_path_ex(argv[2], &limits);
        arc_close(other);
    } else reader = arc_open_path(argv[2]);
    if (!reader) { free(buffer); return 1; }
    int rc = arc_extract_to_path(reader,argv[3],true,true);
    arc_close(reader);
    free(buffer);
    return rc ? 1 : 0;
}
