#include "test_runner.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <errno.h>
#include <dirent.h>

#define TEST_DIR_PREFIX "/tmp/cupidfm_test_"

// Test helper: Create a temporary test directory
static char* create_test_dir(const char *suffix) {
    char *test_dir = malloc(512);
    snprintf(test_dir, 512, "%s%s_%d", TEST_DIR_PREFIX, suffix, getpid());
    mkdir(test_dir, 0755);
    return test_dir;
}

// Test helper: Clean up test directory
static void cleanup_test_dir(const char *test_dir) {
    char command[1024];
    snprintf(command, sizeof(command), "rm -rf \"%s\"", test_dir);
    system(command);
}

// Test helper: Check if file exists
static bool file_exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0;
}

// Test helper: Check if directory exists
static bool dir_exists(const char *path) {
    struct stat st;
    return (stat(path, &st) == 0 && S_ISDIR(st.st_mode));
}

// Test helper: Get file size (kept for future use)
__attribute__((unused))
static long get_file_size(const char *path) {
    struct stat st;
    if (stat(path, &st) == 0) {
        return st.st_size;
    }
    return -1;
}

// Test helper: Read file contents
static bool read_file_contents(const char *path, char *buffer, size_t buffer_size) {
    FILE *f = fopen(path, "r");
    if (!f) return false;
    size_t read = fread(buffer, 1, buffer_size - 1, f);
    buffer[read] = '\0';
    fclose(f);
    return true;
}

// Test helper: Write file contents
static bool write_file_contents(const char *path, const char *content) {
    FILE *f = fopen(path, "w");
    if (!f) return false;
    fprintf(f, "%s", content);
    fclose(f);
    return true;
}

// Test helper: Count files in directory
static int count_files_in_dir(const char *dir_path) {
    DIR *dir = opendir(dir_path);
    if (!dir) return -1;
    
    int count = 0;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0) {
            count++;
        }
    }
    closedir(dir);
    return count;
}

// Test 1: Create a new file
bool test_create_file() {
    char *test_dir = create_test_dir("create_file");
    char test_file[512];
    snprintf(test_file, sizeof(test_file), "%s/test_file.txt", test_dir);
    
    // Create file using standard C library
    FILE *f = fopen(test_file, "w");
    if (!f) {
        cleanup_test_dir(test_dir);
        free(test_dir);
        return false;
    }
    fclose(f);
    
    bool exists = file_exists(test_file);
    cleanup_test_dir(test_dir);
    free(test_dir);
    
    ASSERT_TRUE(exists, "File should be created");
    return true;
}

// Test 2: Delete a file
bool test_delete_file() {
    char *test_dir = create_test_dir("delete_file");
    char test_file[512];
    snprintf(test_file, sizeof(test_file), "%s/test_file.txt", test_dir);
    
    // Create file first
    FILE *f = fopen(test_file, "w");
    if (!f) {
        cleanup_test_dir(test_dir);
        free(test_dir);
        return false;
    }
    fprintf(f, "test content");
    fclose(f);
    
    ASSERT_TRUE(file_exists(test_file), "File should exist before deletion");
    
    // Delete file
    int result = unlink(test_file);
    ASSERT_EQ(result, 0, "unlink should succeed");
    
    bool exists = file_exists(test_file);
    cleanup_test_dir(test_dir);
    free(test_dir);
    
    ASSERT_FALSE(exists, "File should not exist after deletion");
    return true;
}

// Test 3: Rename a file
bool test_rename_file() {
    char *test_dir = create_test_dir("rename_file");
    char old_path[512];
    char new_path[512];
    snprintf(old_path, sizeof(old_path), "%s/old_name.txt", test_dir);
    snprintf(new_path, sizeof(new_path), "%s/new_name.txt", test_dir);
    
    // Create file with old name
    write_file_contents(old_path, "test content");
    ASSERT_TRUE(file_exists(old_path), "Old file should exist");
    
    // Rename file
    int result = rename(old_path, new_path);
    ASSERT_EQ(result, 0, "rename should succeed");
    
    ASSERT_FALSE(file_exists(old_path), "Old file should not exist after rename");
    ASSERT_TRUE(file_exists(new_path), "New file should exist after rename");
    
    // Verify content is preserved
    char content[256];
    read_file_contents(new_path, content, sizeof(content));
    ASSERT_STR_EQ(content, "test content", "File content should be preserved after rename");
    
    cleanup_test_dir(test_dir);
    free(test_dir);
    return true;
}

// Test 4: Create a directory
bool test_create_directory() {
    char *test_dir = create_test_dir("create_dir");
    char new_dir[512];
    snprintf(new_dir, sizeof(new_dir), "%s/new_subdir", test_dir);
    
    int result = mkdir(new_dir, 0755);
    ASSERT_EQ(result, 0, "mkdir should succeed");
    
    bool exists = dir_exists(new_dir);
    cleanup_test_dir(test_dir);
    free(test_dir);
    
    ASSERT_TRUE(exists, "Directory should be created");
    return true;
}

// Test 5: Delete a directory
bool test_delete_directory() {
    char *test_dir = create_test_dir("delete_dir");
    char subdir[512];
    snprintf(subdir, sizeof(subdir), "%s/subdir", test_dir);
    
    // Create directory
    mkdir(subdir, 0755);
    ASSERT_TRUE(dir_exists(subdir), "Directory should exist before deletion");
    
    // Delete directory
    int result = rmdir(subdir);
    ASSERT_EQ(result, 0, "rmdir should succeed");
    
    bool exists = dir_exists(subdir);
    cleanup_test_dir(test_dir);
    free(test_dir);
    
    ASSERT_FALSE(exists, "Directory should not exist after deletion");
    return true;
}

// Test 6: Copy a file
bool test_copy_file() {
    char *test_dir = create_test_dir("copy_file");
    char source[512];
    char dest[512];
    snprintf(source, sizeof(source), "%s/source.txt", test_dir);
    snprintf(dest, sizeof(dest), "%s/dest.txt", test_dir);
    
    // Create source file
    write_file_contents(source, "original content");
    ASSERT_TRUE(file_exists(source), "Source file should exist");
    
    // Copy file using system command (simulating copy_to_clipboard/paste)
    char command[1024];
    snprintf(command, sizeof(command), "cp \"%s\" \"%s\"", source, dest);
    int result = system(command);
    ASSERT_EQ(result, 0, "cp command should succeed");
    
    ASSERT_TRUE(file_exists(dest), "Destination file should exist");
    
    // Verify content
    char content[256];
    read_file_contents(dest, content, sizeof(content));
    ASSERT_STR_EQ(content, "original content", "Copied file should have same content");
    
    // Source should still exist
    ASSERT_TRUE(file_exists(source), "Source file should still exist after copy");
    
    cleanup_test_dir(test_dir);
    free(test_dir);
    return true;
}

// Test 7: Move a file
bool test_move_file() {
    char *test_dir = create_test_dir("move_file");
    char source[512];
    char dest[512];
    snprintf(source, sizeof(source), "%s/source.txt", test_dir);
    snprintf(dest, sizeof(dest), "%s/dest.txt", test_dir);
    
    // Create source file
    write_file_contents(source, "content to move");
    ASSERT_TRUE(file_exists(source), "Source file should exist");
    
    // Move file
    int result = rename(source, dest);
    ASSERT_EQ(result, 0, "rename (move) should succeed");
    
    ASSERT_FALSE(file_exists(source), "Source file should not exist after move");
    ASSERT_TRUE(file_exists(dest), "Destination file should exist after move");
    
    // Verify content
    char content[256];
    read_file_contents(dest, content, sizeof(content));
    ASSERT_STR_EQ(content, "content to move", "Moved file should have same content");
    
    cleanup_test_dir(test_dir);
    free(test_dir);
    return true;
}

// Test 8: File operations with special characters in names
bool test_file_special_characters() {
    char *test_dir = create_test_dir("special_chars");
    char test_file[512];
    snprintf(test_file, sizeof(test_file), "%s/file with spaces.txt", test_dir);
    
    // Create file with spaces in name
    write_file_contents(test_file, "test");
    ASSERT_TRUE(file_exists(test_file), "File with spaces should be created");
    
    // Rename to file with special characters
    char new_file[512];
    snprintf(new_file, sizeof(new_file), "%s/file-with-dashes_123.txt", test_dir);
    int result = rename(test_file, new_file);
    ASSERT_EQ(result, 0, "Rename with special chars should succeed");
    
    ASSERT_TRUE(file_exists(new_file), "Renamed file should exist");
    
    cleanup_test_dir(test_dir);
    free(test_dir);
    return true;
}

// Test 9: Multiple file operations in sequence
bool test_multiple_operations() {
    char *test_dir = create_test_dir("multiple_ops");
    
    // Create multiple files
    for (int i = 0; i < 5; i++) {
        char file[512];
        snprintf(file, sizeof(file), "%s/file%d.txt", test_dir, i);
        write_file_contents(file, "content");
        ASSERT_TRUE(file_exists(file), "File should be created");
    }
    
    // Verify all files exist
    int count = count_files_in_dir(test_dir);
    ASSERT_EQ(count, 5, "Should have 5 files");
    
    // Delete one file
    char file_to_delete[512];
    snprintf(file_to_delete, sizeof(file_to_delete), "%s/file2.txt", test_dir);
    unlink(file_to_delete);
    
    count = count_files_in_dir(test_dir);
    ASSERT_EQ(count, 4, "Should have 4 files after deletion");
    
    // Rename one file
    char old_name[512];
    char new_name[512];
    snprintf(old_name, sizeof(old_name), "%s/file0.txt", test_dir);
    snprintf(new_name, sizeof(new_name), "%s/renamed_file.txt", test_dir);
    rename(old_name, new_name);
    
    ASSERT_FALSE(file_exists(old_name), "Old file should not exist");
    ASSERT_TRUE(file_exists(new_name), "Renamed file should exist");
    
    cleanup_test_dir(test_dir);
    free(test_dir);
    return true;
}

// Test 10: Directory operations with nested structure
bool test_nested_directory_operations() {
    char *test_dir = create_test_dir("nested");
    char subdir1[512];
    char subdir2[512];
    char file1[512];
    char file2[512];
    
    snprintf(subdir1, sizeof(subdir1), "%s/subdir1", test_dir);
    snprintf(subdir2, sizeof(subdir2), "%s/subdir1/subdir2", test_dir);
    snprintf(file1, sizeof(file1), "%s/subdir1/file1.txt", test_dir);
    snprintf(file2, sizeof(file2), "%s/subdir1/subdir2/file2.txt", test_dir);
    
    // Create nested directories
    mkdir(subdir1, 0755);
    mkdir(subdir2, 0755);
    
    ASSERT_TRUE(dir_exists(subdir1), "Subdirectory 1 should exist");
    ASSERT_TRUE(dir_exists(subdir2), "Subdirectory 2 should exist");
    
    // Create files in nested directories
    write_file_contents(file1, "file1 content");
    write_file_contents(file2, "file2 content");
    
    ASSERT_TRUE(file_exists(file1), "File 1 should exist");
    ASSERT_TRUE(file_exists(file2), "File 2 should exist");
    
    // Delete nested directory (should fail if not empty, or succeed if recursive)
    // For this test, we'll just verify structure
    int count1 = count_files_in_dir(subdir1);
    int count2 = count_files_in_dir(subdir2);
    
    ASSERT_EQ(count1, 2, "Subdir1 should have 2 items (1 file + 1 dir)");
    ASSERT_EQ(count2, 1, "Subdir2 should have 1 file");
    
    cleanup_test_dir(test_dir);
    free(test_dir);
    return true;
}

// Test 11: File permissions
bool test_file_permissions() {
    char *test_dir = create_test_dir("permissions");
    char test_file[512];
    snprintf(test_file, sizeof(test_file), "%s/test.txt", test_dir);
    
    // Create file
    write_file_contents(test_file, "test");
    
    // Check file permissions
    struct stat st;
    int result = stat(test_file, &st);
    ASSERT_EQ(result, 0, "stat should succeed");
    
    // File should be readable
    ASSERT_TRUE((st.st_mode & S_IRUSR) != 0, "File should be readable by owner");
    
    cleanup_test_dir(test_dir);
    free(test_dir);
    return true;
}

// Test 12: Error handling - delete non-existent file
bool test_delete_nonexistent_file() {
    char *test_dir = create_test_dir("error_handling");
    char nonexistent[512];
    snprintf(nonexistent, sizeof(nonexistent), "%s/nonexistent.txt", test_dir);
    
    // Try to delete non-existent file
    // Should fail (return -1) or succeed (file doesn't exist)
    // Either way, file should not exist
    (void)unlink(nonexistent); // Ignore return value
    bool exists = file_exists(nonexistent);
    ASSERT_FALSE(exists, "Non-existent file should not exist");
    
    cleanup_test_dir(test_dir);
    free(test_dir);
    return true;
}

int main() {
    printf("=== Integration Tests for File Operations ===\n\n");
    
    RUN_TEST(test_create_file);
    RUN_TEST(test_delete_file);
    RUN_TEST(test_rename_file);
    RUN_TEST(test_create_directory);
    RUN_TEST(test_delete_directory);
    RUN_TEST(test_copy_file);
    RUN_TEST(test_move_file);
    RUN_TEST(test_file_special_characters);
    RUN_TEST(test_multiple_operations);
    RUN_TEST(test_nested_directory_operations);
    RUN_TEST(test_file_permissions);
    RUN_TEST(test_delete_nonexistent_file);
    
    PRINT_SUMMARY();
}

