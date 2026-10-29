#define _POSIX_C_SOURCE 200809L
#include "cupidarchive.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static ssize_t chunk_read(ArcStream *stream, void *buffer, size_t size) {
    return arc_stream_read(stream->user_data, buffer, size > 3 ? 3 : size);
}
static int chunk_seek(ArcStream *stream, int64_t offset, int whence) {
    return arc_stream_seek(stream->user_data, offset, whence);
}
static int64_t chunk_tell(ArcStream *stream) { return arc_stream_tell(stream->user_data); }
static void chunk_close(ArcStream *stream) { arc_stream_close(stream->user_data); free(stream); }
static ArcStream *chunked(ArcStream *parent) {
    static const struct ArcStreamVtable table = {chunk_read,chunk_seek,chunk_tell,chunk_close};
    ArcStream *stream = calloc(1,sizeof(*stream));
    if (stream) { stream->vtable=&table;stream->user_data=parent; }
    return stream;
}

int main(int argc, char **argv) {
    if (argc < 4) return 2;
    void *buffer = NULL;
    ArcReader *reader;
    if (!strcmp(argv[1], "stream") || !strcmp(argv[1], "chunked")) {
        FILE *fp = fopen(argv[2], "rb");
        if (!fp || fseek(fp,0,SEEK_END)) return 2;
        long size = ftell(fp);
        if (size < 0 || fseek(fp,0,SEEK_SET)) return 2;
        buffer = malloc(size ? (size_t)size : 1);
        if (!buffer || fread(buffer,1,(size_t)size,fp)!=(size_t)size) return 2;
        fclose(fp);
        ArcStream *stream = arc_stream_from_memory(buffer,(size_t)size,0);
        if (!strcmp(argv[1], "chunked")) {
            ArcStream *wrapped = chunked(stream);
            if (!wrapped) { arc_stream_close(stream); free(buffer); return 2; }
            stream = wrapped;
        }
        reader = arc_open_stream(stream);
        if (!reader) arc_stream_close(stream);
    } else if (!strcmp(argv[1], "byte-limit") && argc == 5) {
        ArcLimits limits = {.max_uncompressed_bytes = strtoull(argv[4],NULL,10)};
        reader = arc_open_path_ex(argv[2], &limits);
    } else if (argc == 5) {
        ArcLimits limits = {.max_entries = 1};
        reader = arc_open_path_ex(argv[2], &limits);
        limits.max_entries = 1000;
        ArcReader *other = arc_open_path_ex(argv[2], &limits);
        arc_close(other);
    } else reader = arc_open_path(argv[2]);
    if (!reader) { free(buffer); return 1; }
    if (!strcmp(argv[1],"format")) {
        printf("%d\n",arc_format(reader));
        arc_close(reader);free(buffer);return 0;
    }
    if (!strcmp(argv[1],"inspect")) {
        ArcEntry entry;
        int next;
        while ((next=arc_next(reader,&entry))==0) {
            printf("%s\t%llu\t%u\t%u\t%lld\n",entry.path,(unsigned long long)entry.size,entry.uid,entry.gid,(long long)entry.mtime);
            arc_entry_free(&entry);
        }
        arc_close(reader);free(buffer);return next<0 ? 1 : 0;
    }
    int rc = arc_extract_to_path(reader,argv[3],true,true);
    arc_close(reader);
    free(buffer);
    return rc ? 1 : 0;
}
