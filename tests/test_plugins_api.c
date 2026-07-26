#define _POSIX_C_SOURCE 200809L
#define CUPIDFM_TESTING

#include <pthread.h>

#include "test_runner.h"

#include "../src/core/plugins_api.c"

static bool test_clear_selected_paths_preserves_plugin_state(void) {
    PluginManager pm = {0};

    pm.selected_paths = calloc(1, sizeof(char *));
    ASSERT_NOT_NULL(pm.selected_paths, "Selection array should allocate");
    pm.selected_paths[0] = strdup("/tmp/selected");
    ASSERT_NOT_NULL(pm.selected_paths[0], "Selection path should allocate");
    pm.selected_path_count = 1;

    pm.event_bindings = calloc(1, sizeof(EventBinding));
    ASSERT_NOT_NULL(pm.event_bindings, "Event bindings should allocate");
    pm.event_bind_count = 1;
    pm.event_bind_cap = 1;

    pm.marks = calloc(1, sizeof(MarkEntry));
    ASSERT_NOT_NULL(pm.marks, "Marks should allocate");
    pm.marks[0].name = strdup("home");
    pm.marks[0].path = strdup("/tmp");
    ASSERT_NOT_NULL(pm.marks[0].name, "Mark name should allocate");
    ASSERT_NOT_NULL(pm.marks[0].path, "Mark path should allocate");
    pm.mark_count = 1;
    pm.mark_cap = 1;

    EventBinding *bindings = pm.event_bindings;
    MarkEntry *marks = pm.marks;

    selected_paths_clear(&pm);

    ASSERT_NULL(pm.selected_paths, "Selection storage should be released");
    ASSERT_EQ(pm.selected_path_count, 0, "Selection count should reset");
    ASSERT_EQ(pm.event_bindings, bindings, "Event bindings must be preserved");
    ASSERT_EQ(pm.event_bind_count, 1, "Event binding count must be preserved");
    ASSERT_EQ(pm.event_bind_cap, 1, "Event binding capacity must be preserved");
    ASSERT_EQ(pm.marks, marks, "Marks must be preserved");
    ASSERT_EQ(pm.mark_count, 1, "Mark count must be preserved");
    ASSERT_EQ(pm.mark_cap, 1, "Mark capacity must be preserved");

    free(pm.event_bindings);
    free(pm.marks[0].name);
    free(pm.marks[0].path);
    free(pm.marks);
    return true;
}

static int open_fd_count(void) {
    int count = 0;
    for (int fd = 0; fd < 1024; fd++) {
        if (fcntl(fd, F_GETFD) != -1 || errno != EBADF)
            count++;
    }
    return count;
}

static int pipe_call_count;

static int fail_second_pipe(int pipefd[2]) {
    pipe_call_count++;
    if (pipe_call_count == 1)
        return pipe(pipefd);
    errno = EMFILE;
    return -1;
}

static pid_t fail_fork(void) {
    errno = EAGAIN;
    return -1;
}

static bool test_exec_closes_partial_pipe_failure(void) {
    int before_count = open_fd_count();
    pipe_call_count = 0;
    exec_pipe_call = fail_second_pipe;

    PluginManager pm = {0};
    cs_value command = cs_str(NULL, "true");
    cs_value out = cs_nil();
    errno = 0;
    int rc = nf_fm_exec(NULL, &pm, 1, &command, &out);
    int pipe_errno = errno;
    exec_pipe_call = pipe;

    int after_count = open_fd_count();
    cs_value_release(command);
    cs_value_release(out);

    ASSERT_EQ(rc, 0, "Execution setup failure should return cleanly");
    ASSERT_EQ(pipe_errno, EMFILE, "Pipe setup should fail with EMFILE");
    ASSERT_EQ(after_count, before_count,
              "Partial pipe setup must not leak descriptors");
    return true;
}

static bool test_exec_reports_fork_failure_without_leaking(void) {
    int before_count = open_fd_count();
    exec_fork_call = fail_fork;

    PluginManager pm = {0};
    cs_value command = cs_str(NULL, "true");
    cs_value out = cs_nil();
    errno = 0;
    int rc = nf_fm_exec(NULL, &pm, 1, &command, &out);
    int fork_errno = errno;
    exec_fork_call = fork;

    int after_count = open_fd_count();
    cs_value_release(command);

    ASSERT_EQ(rc, 0, "Fork failure should return cleanly");
    ASSERT_EQ(fork_errno, EAGAIN, "Fork should fail with EAGAIN");
    ASSERT_EQ(out.type, CS_T_NIL, "Fork failure should not report success");
    ASSERT_EQ(after_count, before_count, "Fork failure must not leak descriptors");
    cs_value_release(out);
    return true;
}

int main(void) {
    printf("=== Plugin API Tests ===\n\n");

    RUN_TEST(test_clear_selected_paths_preserves_plugin_state);
    RUN_TEST(test_exec_closes_partial_pipe_failure);
    RUN_TEST(test_exec_reports_fork_failure_without_leaking);

    PRINT_SUMMARY();
}
