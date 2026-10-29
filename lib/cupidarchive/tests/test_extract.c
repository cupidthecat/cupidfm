#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "cupidarchive.h"

int main(int argc, char *argv[]) {
    const char *archive_path = "/home/frank/cupidfm/mime_demo/mime_test/test.tar";
    const char *dest_dir = "/home/frank/cupidfm";
    
    // Allow override via command line
    if (argc >= 2) {
        archive_path = argv[1];
    }
    if (argc >= 3) {
        dest_dir = argv[2];
    }
    
    printf("CupidArchive Extraction Test\n");
    printf("============================\n");
    printf("Archive: %s\n", archive_path);
    printf("Destination: %s\n", dest_dir);
    printf("\n");
    
    // List entries first (using a separate reader)
    printf("Archive contents:\n");
    printf("-----------------\n");
    ArcEntry entry;
    int entry_count = 0;
    
    ArcReader *list_reader = arc_open_path(archive_path);
    if (!list_reader) {
        fprintf(stderr, "Error: Failed to open archive '%s': %s\n", archive_path, strerror(errno));
        return 1;
    }
    printf("✓ Archive opened successfully\n\n");
    
    // First pass: list entries
    while (arc_next(list_reader, &entry) == 0) {
        const char *type_str = "?";
        switch (entry.type) {
            case ARC_ENTRY_FILE: type_str = "FILE"; break;
            case ARC_ENTRY_DIR: type_str = "DIR "; break;
            case ARC_ENTRY_SYMLINK: type_str = "LINK"; break;
            case ARC_ENTRY_HARDLINK: type_str = "HLNK"; break;
            default: type_str = "????"; break;
        }
        
        printf("  [%s] %s", type_str, entry.path);
        if (entry.type == ARC_ENTRY_FILE) {
            printf(" (%lu bytes)", entry.size);
        } else if (entry.type == ARC_ENTRY_SYMLINK && entry.link_target) {
            printf(" -> %s", entry.link_target);
        }
        printf("\n");
        
        arc_entry_free(&entry);
        entry_count++;
    }
    arc_close(list_reader);
    
    printf("\nTotal entries: %d\n\n", entry_count);
    
    // Extract archive (using a fresh reader)
    printf("Extracting archive...\n");
    printf("(preserving permissions and timestamps)\n");
    
    ArcReader *reader = arc_open_path(archive_path);
    if (!reader) {
        fprintf(stderr, "Error: Failed to open archive for extraction: %s\n", strerror(errno));
        return 1;
    }
    
    int result = arc_extract_to_path(reader, dest_dir, true, true);
    
    if (result == 0) {
        printf("\n✓ Extraction completed successfully!\n");
        printf("Files extracted to: %s\n", dest_dir);
    } else {
        fprintf(stderr, "\n✗ Extraction failed: %s\n", strerror(errno));
        arc_close(reader);
        return 1;
    }
    
    arc_close(reader);
    
    printf("\nDone!\n");
    return 0;
}

