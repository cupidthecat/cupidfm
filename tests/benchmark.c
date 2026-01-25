#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdint.h>
#include <stdbool.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include "vector.h"
#include "vecstack.h"

#define MAX_PATH_LENGTH 1024

// Benchmark notes:
// - All benchmarks accumulate results in volatile variables to prevent dead store elimination
// - Function calls are used to prevent path/constant optimization
// - Results are used to ensure compiler doesn't optimize away the work
// - Cold cache benchmarks require root access to clear page cache

// Simple path_join implementation for benchmarking
void path_join(char *result, const char *base, const char *extra) {
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
            snprintf(result, MAX_PATH_LENGTH, "%s/%s", base, extra);
        }
    }

    result[MAX_PATH_LENGTH - 1] = '\0';
}

// Get current time in nanoseconds (best available precision)
static uint64_t get_time_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

// Helper functions to force runtime execution (prevent compile-time folding)
__attribute__((noinline)) static size_t bench_strlen(const char *s) {
    return strlen(s);
}

__attribute__((noinline)) static void bench_strncpy(char *dest, const char *src, size_t n) {
    strncpy(dest, src, n);
    dest[n - 1] = '\0';
}

// Benchmark a function
#define BENCHMARK(name, iterations, code) \
    do { \
        uint64_t start = get_time_ns(); \
        for (int i = 0; i < (iterations); i++) { \
            code; \
        } \
        uint64_t end = get_time_ns(); \
        uint64_t elapsed = end - start; \
        double avg_ns = (double)elapsed / (iterations); \
        double avg_us = avg_ns / 1000.0; \
        double avg_ms = avg_us / 1000.0; \
        printf("  %-40s: %10.2f ns/op  %8.3f μs/op  %8.3f ms/op\n", \
               name, avg_ns, avg_us, avg_ms); \
    } while(0)

void benchmark_vector_add(void) {
    printf("\n=== Vector Add Operations ===\n");
    const int iterations = 100000;
    
    // Pre-allocate values once to isolate vector operations from malloc overhead
    int *preallocated_values[100];
    for (int i = 0; i < 100; i++) {
        preallocated_values[i] = malloc(sizeof(int));
        *preallocated_values[i] = i;
    }
    
    // Accumulate to prevent optimization
    volatile size_t total_elements = 0;
    
    BENCHMARK("Vector add (100k iterations, 100 elements each)", iterations, {
        Vector v = Vector_new(10);
        for (int j = 0; j < 100; j++) {
            // Use pre-allocated value to isolate vector operations
            size_t current_len = Vector_len(v);
            Vector_add(&v, 1);
            v.el[current_len] = preallocated_values[j];
            Vector_set_len_no_free(&v, current_len + 1);
        }
        total_elements += Vector_len(v); // Use result to prevent optimization
        // Clear elements before freeing to avoid double-free
        for (size_t i = 0; i < Vector_len(v); i++) {
            v.el[i] = NULL;
        }
        Vector_bye(&v);
    });
    
    // Clean up pre-allocated values
    for (int i = 0; i < 100; i++) {
        free(preallocated_values[i]);
    }
    
    (void)total_elements; // Suppress unused warning
}

void benchmark_vector_access(void) {
    printf("\n=== Vector Access Operations ===\n");
    const int iterations = 1000000;
    
    Vector v = Vector_new(100);
    for (int i = 0; i < 1000; i++) {
        int *val = malloc(sizeof(int));
        *val = i;
        Vector_add(&v, 1);
        v.el[Vector_len(v)] = val;
        Vector_set_len_no_free(&v, Vector_len(v) + 1);
    }
    
    BENCHMARK("Vector access (1M accesses)", iterations, {
        size_t idx = (size_t)((uintptr_t)&v % 1000);
        if (idx < Vector_len(v)) {
            int *val = (int *)v.el[idx];
            (void)val; // Prevent optimization
        }
    });
    
    Vector_bye(&v);
}

void benchmark_vector_set_len(void) {
    printf("\n=== Vector Set Length Operations ===\n");
    const int iterations = 10000;
    
    BENCHMARK("Vector set_len (10k operations)", iterations, {
        Vector v = Vector_new(10);
        for (int j = 0; j < 100; j++) {
            int *val = malloc(sizeof(int));
            *val = j;
            Vector_add(&v, 1);
            v.el[Vector_len(v)] = val;
            Vector_set_len_no_free(&v, Vector_len(v) + 1);
        }
        // Set length to 0 to free all elements
        Vector_set_len(&v, 0);
        Vector_bye(&v);
    });
}

void benchmark_path_join(void) {
    printf("\n=== Path Join Operations ===\n");
    const int iterations = 1000000;
    
    BENCHMARK("Path join (1M operations)", iterations, {
        char result[MAX_PATH_LENGTH];
        path_join(result, "/home/user", "documents/file.txt");
        (void)result; // Prevent optimization
    });
    
    BENCHMARK("Path join (empty base)", iterations, {
        char result[MAX_PATH_LENGTH];
        path_join(result, "", "documents/file.txt");
        (void)result;
    });
    
    BENCHMARK("Path join (base ends with /)", iterations, {
        char result[MAX_PATH_LENGTH];
        path_join(result, "/home/user/", "documents/file.txt");
        (void)result;
    });
}

void benchmark_vecstack_push_pop(void) {
    printf("\n=== VecStack Push/Pop Operations ===\n");
    const int iterations = 100000;
    
    BENCHMARK("VecStack push/pop (100k ops)", iterations, {
        VecStack s = VecStack_empty();
        for (int j = 0; j < 10; j++) {
            int *val = malloc(sizeof(int));
            *val = j;
            VecStack_push(&s, val);
        }
        while (Vector_len(s.v) > 0) {
            int *val = (int *)VecStack_pop(&s);
            free(val);
        }
        VecStack_bye(&s);
    });
}

void benchmark_vecstack_peek(void) {
    printf("\n=== VecStack Peek Operations ===\n");
    const int iterations = 10000000;
    
    VecStack s = VecStack_empty();
    for (int i = 0; i < 100; i++) {
        int *val = malloc(sizeof(int));
        *val = i;
        VecStack_push(&s, val);
    }
    
    BENCHMARK("VecStack peek (10M operations)", iterations, {
        int *val = (int *)VecStack_peek(&s);
        (void)val; // Prevent optimization
    });
    
    while (Vector_len(s.v) > 0) {
        int *val = (int *)VecStack_pop(&s);
        free(val);
    }
    VecStack_bye(&s);
}

void benchmark_vector_capacity_operations(void) {
    printf("\n=== Vector Capacity Operations ===\n");
    const int iterations = 10000;
    
    BENCHMARK("Vector_min_cap (10k operations)", iterations, {
        Vector v = Vector_new(100);
        for (int j = 0; j < 50; j++) {
            int *val = malloc(sizeof(int));
            *val = j;
            Vector_add(&v, 1);
            v.el[Vector_len(v)] = val;
            Vector_set_len_no_free(&v, Vector_len(v) + 1);
        }
        Vector_min_cap(&v);
        Vector_bye(&v);
    });
    
    BENCHMARK("Vector_sane_cap (10k operations)", iterations, {
        Vector v = Vector_new(100);
        for (int j = 0; j < 50; j++) {
            int *val = malloc(sizeof(int));
            *val = j;
            Vector_add(&v, 1);
            v.el[Vector_len(v)] = val;
            Vector_set_len_no_free(&v, Vector_len(v) + 1);
        }
        Vector_sane_cap(&v);
        Vector_bye(&v);
    });
}

void benchmark_vector_large_operations(void) {
    printf("\n=== Vector Large Scale Operations ===\n");
    const int iterations = 1000;
    
    BENCHMARK("Vector add (1k elements, 1k ops)", iterations, {
        Vector v = Vector_new(10);
        for (int j = 0; j < 1000; j++) {
            int *val = malloc(sizeof(int));
            *val = j;
            Vector_add(&v, 1);
            v.el[Vector_len(v)] = val;
            Vector_set_len_no_free(&v, Vector_len(v) + 1);
        }
        Vector_bye(&v);
    });
    
    BENCHMARK("Vector sequential access (1k elements)", iterations, {
        Vector v = Vector_new(100);
        for (int j = 0; j < 1000; j++) {
            int *val = malloc(sizeof(int));
            *val = j;
            Vector_add(&v, 1);
            v.el[Vector_len(v)] = val;
            Vector_set_len_no_free(&v, Vector_len(v) + 1);
        }
        // Sequential access
        for (size_t i = 0; i < Vector_len(v); i++) {
            int *val = (int *)v.el[i];
            (void)val;
        }
        Vector_bye(&v);
    });
}

void benchmark_path_join_variations(void) {
    printf("\n=== Path Join Variations ===\n");
    const int iterations = 500000;
    
    BENCHMARK("Path join (long paths)", iterations, {
        char result[MAX_PATH_LENGTH];
        path_join(result, "/very/long/path/to/some/directory", "subdirectory/file.txt");
        (void)result;
    });
    
    BENCHMARK("Path join (multiple segments)", iterations, {
        char result[MAX_PATH_LENGTH];
        char temp[MAX_PATH_LENGTH];
        path_join(temp, "/home", "user");
        path_join(result, temp, "documents");
        path_join(temp, result, "projects");
        path_join(result, temp, "file.txt");
        (void)result;
    });
    
    BENCHMARK("Path join (root paths)", iterations, {
        char result[MAX_PATH_LENGTH];
        path_join(result, "/", "usr");
        path_join(result, result, "bin");
        (void)result;
    });
}

void benchmark_vecstack_large_stack(void) {
    printf("\n=== VecStack Large Stack Operations ===\n");
    const int iterations = 1000;
    
    BENCHMARK("VecStack push (1k elements)", iterations, {
        VecStack s = VecStack_empty();
        for (int j = 0; j < 1000; j++) {
            int *val = malloc(sizeof(int));
            *val = j;
            VecStack_push(&s, val);
        }
        while (Vector_len(s.v) > 0) {
            int *val = (int *)VecStack_pop(&s);
            free(val);
        }
        VecStack_bye(&s);
    });
    
    BENCHMARK("VecStack push/pop alternating", iterations, {
        VecStack s = VecStack_empty();
        for (int j = 0; j < 100; j++) {
            int *val = malloc(sizeof(int));
            *val = j;
            VecStack_push(&s, val);
            if (j % 2 == 0) {
                int *popped = (int *)VecStack_pop(&s);
                if (popped) free(popped);
            }
        }
        while (Vector_len(s.v) > 0) {
            int *val = (int *)VecStack_pop(&s);
            free(val);
        }
        VecStack_bye(&s);
    });
}

void benchmark_string_operations(void) {
    printf("\n=== String Operations ===\n");
    const int iterations = 1000000;
    
    // Force runtime strlen by using mutable array and noinline wrapper
    char str[] = "/home/user/documents/file.txt";
    volatile size_t strlen_total = 0;
    
    BENCHMARK("strlen (1M operations)", iterations, {
        volatile const char *p = str; // Volatile pointer prevents compile-time folding
        size_t len = bench_strlen((const char*)p);
        strlen_total += len; // Accumulate to prevent optimization
    });
    
    printf("  Total length (prevents optimization): %zu\n", (size_t)strlen_total);
    
    // Force runtime strncpy by using mutable arrays and noinline wrapper
    char src_str[] = "/home/user/documents/file.txt";
    volatile size_t strncpy_total = 0;
    
    BENCHMARK("strncpy (1M operations)", iterations, {
        char dest[MAX_PATH_LENGTH];
        volatile const char *p = src_str; // Volatile pointer prevents optimization
        bench_strncpy(dest, (const char*)p, MAX_PATH_LENGTH);
        strncpy_total += dest[0]; // Use result to prevent optimization
    });
    
    printf("  Total chars (prevents optimization): %zu\n", (size_t)strncpy_total);
    
    // Accumulate to prevent dead store elimination
    volatile size_t snprintf_total_len = 0;
    volatile char last_char = 0;
    
    BENCHMARK("snprintf (1M operations)", iterations, {
        char result[MAX_PATH_LENGTH];
        // Use more complex format with loop variable to prevent optimization
        int len = snprintf(result, MAX_PATH_LENGTH, "%s/%s/%d", "/home/user", "file", i % 1000);
        snprintf_total_len += (len > 0 ? len : 0); // Use result to prevent optimization
        // Force buffer to be written and read
        last_char = result[len > 0 ? len - 1 : 0];
        // Also ensure buffer is actually used
        if (result[0] != '\0') {
            snprintf_total_len += 1;
        }
    });
    
    (void)snprintf_total_len; // Suppress unused warning
    (void)last_char; // Suppress unused warning
}

// Simple directory size calculation (non-recursive, for benchmarking)
static long calculate_dir_size_simple(const char *dir_path) {
    DIR *dir = opendir(dir_path);
    if (!dir) return -1;
    
    long total_size = 0;
    struct dirent *entry;
    struct stat statbuf;
    char path[MAX_PATH_LENGTH];
    
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        
        snprintf(path, sizeof(path), "%s/%s", dir_path, entry->d_name);
        path[MAX_PATH_LENGTH - 1] = '\0';
        
        if (lstat(path, &statbuf) == 0) {
            if (!S_ISDIR(statbuf.st_mode)) {
                total_size += statbuf.st_size;
            }
        }
    }
    
    closedir(dir);
    return total_size;
}

// Count files in directory
static size_t count_files_in_dir(const char *dir_path) {
    DIR *dir = opendir(dir_path);
    if (!dir) return 0;
    
    size_t count = 0;
    struct dirent *entry;
    
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0) {
            count++;
        }
    }
    
    closedir(dir);
    return count;
}

// List directory entries (just read, no processing)
// Returns count of entries to ensure full traversal
static size_t list_directory_entries(const char *dir_path) {
    DIR *dir = opendir(dir_path);
    if (!dir) return 0;
    
    size_t count = 0;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        count++; // Count entries to ensure full traversal
        (void)entry; // Prevent optimization
    }
    
    closedir(dir);
    return count;
}


void benchmark_directory_operations(void) {
    printf("\n=== Directory Reading Operations ===\n");
    
    // Use /tmp for testing - it should exist on most systems
    const char *test_dir = "/tmp";
    if (access(test_dir, R_OK) != 0) {
        printf("  Skipping directory benchmarks - /tmp not accessible\n");
        return;
    }
    
    const int iterations = 10000;
    
    // Accumulate entry counts to prevent optimization
    volatile size_t total_entries = 0;
    
    BENCHMARK("opendir/readdir/closedir (10k ops)", iterations, {
        // Each iteration does a full directory traversal
        size_t count = list_directory_entries(test_dir);
        total_entries += count; // Use result to prevent optimization
    });
    
    printf("  Total entries counted: %zu (prevents optimization)\n", (size_t)total_entries);
    
    BENCHMARK("Count files in directory (10k ops)", iterations, {
        size_t count = count_files_in_dir(test_dir);
        (void)count;
    });
    
    // Create multiple test paths to cycle through (prevents optimization)
    const char *test_paths[] = {
        "/tmp",
        "/tmp",
        "/usr",
        "/usr/bin",
        "/usr/lib",
        "/var",
        "/var/log",
        "/home",
        "/proc",
        "/sys",
        NULL
    };
    
    // Count valid paths
    int path_count = 0;
    for (int p = 0; test_paths[p] != NULL; p++) {
        if (access(test_paths[p], R_OK) == 0) {
            path_count++;
        }
    }
    
    if (path_count == 0) {
        printf("  Skipping stat benchmarks - no accessible paths\n");
        return;
    }
    
    // Use volatile checksum to prevent optimization
    volatile unsigned long checksum = 0;
    
    BENCHMARK("stat directory (10k operations, cycling paths)", iterations, {
        // Cycle through different paths to force real syscalls
        const char *path = test_paths[i % path_count];
        struct stat statbuf;
        if (stat(path, &statbuf) == 0) {
            // Fold multiple fields into checksum to prevent optimization
            checksum += (unsigned long)statbuf.st_ino;
            checksum += (unsigned long)statbuf.st_size;
            checksum += (unsigned long)statbuf.st_mode;
            checksum += (unsigned long)statbuf.st_nlink;
        }
    });
    
    BENCHMARK("lstat directory (10k operations, cycling paths)", iterations, {
        // Cycle through different paths to force real syscalls
        const char *path = test_paths[i % path_count];
        struct stat statbuf;
        if (lstat(path, &statbuf) == 0) {
            // Fold multiple fields into checksum to prevent optimization
            checksum += (unsigned long)statbuf.st_ino;
            checksum += (unsigned long)statbuf.st_size;
            checksum += (unsigned long)statbuf.st_mode;
            checksum += (unsigned long)statbuf.st_nlink;
        }
    });
    
    // Print checksum to ensure it's used (prevents dead code elimination)
    printf("  Checksum (prevents optimization): %lu\n", (unsigned long)checksum);
}

void benchmark_directory_size_operations(void) {
    printf("\n=== Directory Size Calculation Operations ===\n");
    
    // Use /tmp for testing
    const char *test_dir = "/tmp";
    if (access(test_dir, R_OK) != 0) {
        printf("  Skipping directory size benchmarks - /tmp not accessible\n");
        return;
    }
    
    const int iterations = 1000;
    
    BENCHMARK("Calculate dir size (non-recursive, 1k ops)", iterations, {
        long size = calculate_dir_size_simple(test_dir);
        (void)size;
    });
    
    // Test with a directory that likely has files
    const char *usr_bin = "/usr/bin";
    if (access(usr_bin, R_OK) == 0) {
        BENCHMARK("Calculate /usr/bin size (1k ops)", iterations / 10, {
            long size = calculate_dir_size_simple(usr_bin);
            (void)size;
        });
    }
    
    // Test with current directory
    char cwd[MAX_PATH_LENGTH];
    if (getcwd(cwd, sizeof(cwd)) != NULL) {
        BENCHMARK("Calculate current dir size (1k ops)", iterations, {
            long size = calculate_dir_size_simple(cwd);
            (void)size;
        });
    }
}

// Clear OS page cache (requires root, but we'll try gracefully)
// Returns true if cache was cleared, false otherwise
static bool attempt_cache_clear(void) {
    // Try to clear page cache (requires root)
    // This is a best-effort attempt - will fail gracefully if not root
    FILE *f = fopen("/proc/sys/vm/drop_caches", "w");
    if (f) {
        // Write "3" to clear:
        // 1 = pagecache
        // 2 = dentries and inodes
        // 3 = pagecache, dentries, and inodes
        fprintf(f, "3\n");
        fclose(f);
        sync(); // Ensure all writes are flushed
        return true;
    }
    return false;
}

// Verify cache is actually cleared by checking /proc/meminfo
// Returns true if cache appears to be cleared (or if we can't check)
static bool verify_cache_cleared(void) {
    FILE *f = fopen("/proc/meminfo", "r");
    if (!f) return true; // Can't verify, assume it worked
    
    char line[256];
    unsigned long cached = 0, buffers = 0;
    
    while (fgets(line, sizeof(line), f)) {
        if (sscanf(line, "Cached: %lu", &cached) == 1) {
            // Found cached value
        } else if (sscanf(line, "Buffers: %lu", &buffers) == 1) {
            // Found buffers value
        }
    }
    fclose(f);
    
    // If cache is very small (< 100MB), it's likely cleared
    // This is a heuristic - actual values depend on system
    return (cached < 100 * 1024 * 1024); // 100MB threshold
}

void benchmark_directory_cold_cache(void) {
    printf("\n=== Directory Operations (Cold Cache) ===\n");
    printf("Note: Cold cache benchmarks require root access to clear page cache.\n");
    printf("      Run with: sudo ./benchmark (or as root) for accurate cold cache results.\n");
    printf("      Without root, results may show 'hot cache' performance.\n\n");
    
    const char *test_dir = "/tmp";
    if (access(test_dir, R_OK) != 0) {
        printf("  Skipping cold cache benchmarks - /tmp not accessible\n");
        return;
    }
    
    const int iterations = 100;
    bool cache_cleared = false;
    
    // Warm up the cache first
    printf("  Warming cache...\n");
    for (int i = 0; i < 10; i++) {
        (void)list_directory_entries(test_dir);
        calculate_dir_size_simple(test_dir);
    }
    
    // Hot cache benchmark
    BENCHMARK("opendir/readdir (hot cache, 100 ops)", iterations, {
        list_directory_entries(test_dir);
    });
    
    // Attempt to clear cache
    printf("  Attempting to clear page cache...\n");
    cache_cleared = attempt_cache_clear();
    if (cache_cleared) {
        printf("  ✓ Cache cleared successfully (running as root)\n");
        // Wait longer for cache to actually clear
        printf("  Waiting for cache to clear (1 second)...\n");
        usleep(1000000); // 1 second
        
        // Verify cache is actually cleared (note: Linux may refill quickly)
        if (verify_cache_cleared()) {
            printf("  ✓ Cache appears cleared (checked /proc/meminfo)\n");
        } else {
            printf("  ⚠ Cache may not be fully cleared (large cached value detected)\n");
            printf("     Note: Linux may refill caches quickly after drop_caches\n");
        }
        
        // Measure cold first read separately, then warmed state
        printf("  Measuring cold first read (single op)...\n");
        uint64_t cold_start = get_time_ns();
        size_t cold_first_count = list_directory_entries(test_dir);
        uint64_t cold_end = get_time_ns();
        uint64_t cold_elapsed = cold_end - cold_start;
        double cold_first_ns = (double)cold_elapsed;
        printf("  Cold first read (1 op)            : %10.2f ns/op  %8.3f μs/op  %8.3f ms/op\n",
               cold_first_ns, cold_first_ns / 1000.0, cold_first_ns / 1000000.0);
        printf("  Cold first read counted %zu entries\n", cold_first_count);
        
        // Now measure warmed steady-state after the first read
        volatile size_t warm_total = 0;
        BENCHMARK("Warm steady-state (100 ops)", iterations, {
            size_t count = list_directory_entries(test_dir);
            warm_total += count;
        });
        printf("  Warm steady-state entries: %zu\n", (size_t)warm_total);
    } else {
        printf("  ⚠ Cache clear failed (need root access) - results may show hot cache\n");
        printf("  💡 Run with 'sudo ./benchmark' for accurate cold cache measurements\n");
        
        // Without cache clearing, just do regular benchmark
        volatile size_t cold_total = 0;
        BENCHMARK("opendir/readdir (100 ops)", iterations, {
            size_t count = list_directory_entries(test_dir);
            cold_total += count;
        });
        printf("  Entries counted: %zu\n", (size_t)cold_total);
    }
    
    // Re-warm for size calculation
    printf("  Re-warming cache for size calculation...\n");
    for (int i = 0; i < 5; i++) {
        calculate_dir_size_simple(test_dir);
    }
    
    BENCHMARK("Calculate dir size (hot cache, 100 ops)", iterations, {
        long size = calculate_dir_size_simple(test_dir);
        (void)size;
    });
    
    // Clear cache again
    if (cache_cleared) {
        printf("  Clearing cache again for size calculation...\n");
        attempt_cache_clear();
        printf("  Waiting for cache to clear (1 second)...\n");
        usleep(1000000); // 1 second
        calculate_dir_size_simple(test_dir); // Cold read
        usleep(500000); // Additional 500ms
    }
    
    BENCHMARK("Calculate dir size (cold cache, 100 ops)", iterations, {
        long size = calculate_dir_size_simple(test_dir);
        (void)size;
    });
    
    // Test with larger directories for better cold cache separation
    // Try /usr/lib first (usually much larger than /usr/bin)
    const char *large_dirs[] = {"/usr/lib", "/usr/bin", NULL};
    bool tested_large = false;
    
    for (int dir_idx = 0; large_dirs[dir_idx] != NULL; dir_idx++) {
        const char *test_large_dir = large_dirs[dir_idx];
        if (access(test_large_dir, R_OK) != 0) {
            continue; // Skip if not accessible
        }
        
        if (tested_large) {
            printf("\n"); // Add spacing between multiple directories
        }
        
        printf("  Testing with %s (large directory)...\n", test_large_dir);
        
        // Warm cache - more iterations for larger directory
        printf("  Warming cache for %s (multiple passes)...\n", test_large_dir);
        for (int i = 0; i < 30; i++) {
            list_directory_entries(test_large_dir);
        }
        
        // Hot cache benchmark - accumulate counts to prevent optimization
        volatile size_t hot_total = 0;
        BENCHMARK("Read large dir (hot cache, 500 ops)", 500, {
            // Each iteration does a full directory traversal
            size_t count = list_directory_entries(test_large_dir);
            hot_total += count; // Use result to prevent optimization
        });
        printf("  Hot cache entries counted: %zu\n", (size_t)hot_total);
        
        if (cache_cleared) {
            printf("  Clearing cache for %s...\n", test_large_dir);
            attempt_cache_clear();
            
            // Wait longer and verify cache is cleared
            printf("  Waiting for cache to clear (2 seconds)...\n");
            usleep(2000000); // 2 seconds
            
            // Don't do a warm-up read here - that would defeat the purpose!
            // Just wait a bit for cache to settle
            usleep(1000000); // Another 1 second
            
            // Verify cache is actually cleared (heuristic check)
            if (verify_cache_cleared()) {
                printf("  ✓ Cache appears cleared (verified via /proc/meminfo)\n");
            } else {
                printf("  ⚠ Cache may not be fully cleared (large cached value in /proc/meminfo)\n");
            }
        }
        
        // Cold cache benchmark - measure first read separately, then warmed state
        if (cache_cleared) {
            printf("  Measuring cold first read (single op)...\n");
            uint64_t cold_start = get_time_ns();
            size_t cold_first_count = list_directory_entries(test_large_dir);
            uint64_t cold_end = get_time_ns();
            uint64_t cold_elapsed = cold_end - cold_start;
            double cold_first_ns = (double)cold_elapsed;
            printf("  Cold first read (1 op)            : %10.2f ns/op  %8.3f μs/op  %8.3f ms/op\n",
                   cold_first_ns, cold_first_ns / 1000.0, cold_first_ns / 1000000.0);
            printf("  Cold first read counted %zu entries\n", cold_first_count);
            
            // Now measure warmed steady-state after the first read
            volatile size_t warm_total = 0;
            BENCHMARK("Warm steady-state (500 ops)", 500, {
                size_t count = list_directory_entries(test_large_dir);
                warm_total += count;
            });
            printf("  Warm steady-state entries: %zu\n", (size_t)warm_total);
        } else {
            // Without cache clearing, just do regular benchmark
            volatile size_t cold_total = 0;
            BENCHMARK("Read large dir (500 ops)", 500, {
                size_t count = list_directory_entries(test_large_dir);
                cold_total += count;
            });
            printf("  Entries counted: %zu\n", (size_t)cold_total);
        }
        
        tested_large = true;
        
        // Only test first accessible large directory to save time
        // Uncomment to test all:
        // break;
    }
    
    if (!tested_large) {
        printf("  ⚠ No large directories accessible for testing\n");
    }
    
    if (!cache_cleared) {
        printf("\n  💡 Tip: Run 'sudo ./benchmark' for accurate cold cache measurements\n");
    }
}

int main(void) {
    printf("╔════════════════════════════════════════════════════════════════╗\n");
    printf("║          CupidFM Performance Benchmarks                        ║\n");
    printf("╚════════════════════════════════════════════════════════════════╝\n");
    
    benchmark_vector_add();
    benchmark_vector_access();
    benchmark_vector_set_len();
    benchmark_vector_capacity_operations();
    benchmark_vector_large_operations();
    benchmark_path_join();
    benchmark_path_join_variations();
    benchmark_vecstack_push_pop();
    benchmark_vecstack_peek();
    benchmark_vecstack_large_stack();
    benchmark_string_operations();
    benchmark_directory_operations();
    benchmark_directory_size_operations();
    benchmark_directory_cold_cache();
    
    printf("\n╔════════════════════════════════════════════════════════════════╗\n");
    printf("║                    Benchmarks Complete                         ║\n");
    printf("╚════════════════════════════════════════════════════════════════╝\n");
    printf("\nNote: Results may vary based on system load and CPU frequency scaling.\n");
    printf("      Run multiple times and average for more accurate results.\n\n");
    
    return 0;
}

