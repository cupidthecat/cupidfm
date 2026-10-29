#define _POSIX_C_SOURCE 200809L
#include "test_runner.h"
#include "../src/arc_stream.h"
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>

// Test memory stream creation
bool test_stream_from_memory() {
    const char *data = "Hello, World!";
    size_t len = strlen(data);
    
    ArcStream *stream = arc_stream_from_memory(data, len, len);
    ASSERT_NOT_NULL(stream, "Memory stream should be created");
    
    char buf[100];
    memset(buf, 0, sizeof(buf));
    ssize_t n = arc_stream_read(stream, buf, sizeof(buf));
    ASSERT_EQ(n, (ssize_t)len, "Should read all data");
    buf[n] = '\0';
    ASSERT_STR_EQ(buf, data, "Read data should match");
    
    arc_stream_close(stream);
    return true;
}

// Test memory stream byte limit
bool test_stream_byte_limit() {
    const char *data = "Hello, World!";
    size_t len = strlen(data);
    
    // Create stream with limit smaller than data
    ArcStream *stream = arc_stream_from_memory(data, len, 5);
    ASSERT_NOT_NULL(stream, "Memory stream should be created");
    
    char buf[100];
    ssize_t n = arc_stream_read(stream, buf, sizeof(buf));
    ASSERT_EQ(n, 5, "Should respect byte limit");
    
    // Next read should return 0 (EOF) since we've read all available data
    // The byte limit prevents reading beyond the limit, but we've already read
    // all the data that fits within the limit
    n = arc_stream_read(stream, buf, sizeof(buf));
    // After reading up to the limit, we should get EOF (0) or error
    ASSERT_TRUE(n == 0 || n == -1, "Should return EOF or error after limit");
    
    arc_stream_close(stream);
    return true;
}

// Test file descriptor stream
bool test_stream_from_fd() {
    // Create a temporary file
    const char *test_data = "Test file content";
    int fd = open("/tmp/cupidarchive_test.txt", O_CREAT | O_RDWR | O_TRUNC, 0644);
    ASSERT_NE(fd, -1, "Should create test file");
    
    write(fd, test_data, strlen(test_data));
    lseek(fd, 0, SEEK_SET);
    
    ArcStream *stream = arc_stream_from_fd(fd, 1000);
    ASSERT_NOT_NULL(stream, "FD stream should be created");
    
    char buf[100];
    ssize_t n = arc_stream_read(stream, buf, sizeof(buf));
    ASSERT_EQ(n, (ssize_t)strlen(test_data), "Should read file data");
    buf[n] = '\0';
    ASSERT_STR_EQ(buf, test_data, "Read data should match");
    
    arc_stream_close(stream);
    close(fd);
    unlink("/tmp/cupidarchive_test.txt");
    return true;
}

// Test stream seek
bool test_stream_seek() {
    const char *data = "Hello, World!";
    size_t len = strlen(data);
    
    ArcStream *stream = arc_stream_from_memory(data, len, len);
    ASSERT_NOT_NULL(stream, "Memory stream should be created");
    
    // Seek to beginning
    int result = arc_stream_seek(stream, 0, SEEK_SET);
    ASSERT_EQ(result, 0, "Seek to beginning should succeed");
    
    // Seek to middle
    result = arc_stream_seek(stream, 7, SEEK_SET);
    ASSERT_EQ(result, 0, "Seek to middle should succeed");
    
    char buf[100];
    ssize_t n = arc_stream_read(stream, buf, sizeof(buf));
    ASSERT_EQ(n, 6, "Should read remaining data");
    buf[n] = '\0';
    ASSERT_STR_EQ(buf, "World!", "Should read from seek position");
    
    arc_stream_close(stream);
    return true;
}

// Test stream tell
bool test_stream_tell() {
    const char *data = "Hello, World!";
    size_t len = strlen(data);
    
    ArcStream *stream = arc_stream_from_memory(data, len, len);
    ASSERT_NOT_NULL(stream, "Memory stream should be created");
    
    int64_t pos = arc_stream_tell(stream);
    ASSERT_EQ(pos, 0, "Initial position should be 0");
    
    char buf[5];
    arc_stream_read(stream, buf, sizeof(buf));
    
    pos = arc_stream_tell(stream);
    ASSERT_EQ(pos, 5, "Position should advance after read");
    
    arc_stream_close(stream);
    return true;
}

// Test substream
bool test_substream() {
    const char *data = "Hello, World! This is a longer string.";
    size_t len = strlen(data);
    
    ArcStream *parent = arc_stream_from_memory(data, len, len);
    ASSERT_NOT_NULL(parent, "Parent stream should be created");
    
    // Create substream from offset 7, length 5
    ArcStream *sub = arc_stream_substream(parent, 7, 5);
    ASSERT_NOT_NULL(sub, "Substream should be created");
    
    char buf[100];
    ssize_t n = arc_stream_read(sub, buf, sizeof(buf));
    ASSERT_EQ(n, 5, "Should read substream data");
    buf[n] = '\0';
    ASSERT_STR_EQ(buf, "World", "Should read correct substream data");
    
    arc_stream_close(sub);
    arc_stream_close(parent);
    return true;
}

// Test null stream handling
bool test_stream_null_handling() {
    ssize_t n = arc_stream_read(NULL, NULL, 0);
    ASSERT_EQ(n, -1, "Read from NULL stream should fail");
    ASSERT_EQ(errno, EINVAL, "Should set EINVAL");
    
    int result = arc_stream_seek(NULL, 0, SEEK_SET);
    ASSERT_EQ(result, -1, "Seek on NULL stream should fail");
    
    int64_t pos = arc_stream_tell(NULL);
    ASSERT_EQ(pos, -1, "Tell on NULL stream should fail");
    
    arc_stream_close(NULL); // Should not crash
    return true;
}

int main() {
    printf("=== ArcStream Tests ===\n\n");
    
    RUN_TEST(test_stream_from_memory);
    RUN_TEST(test_stream_byte_limit);
    RUN_TEST(test_stream_from_fd);
    RUN_TEST(test_stream_seek);
    RUN_TEST(test_stream_tell);
    RUN_TEST(test_substream);
    RUN_TEST(test_stream_null_handling);
    
    PRINT_SUMMARY();
}

