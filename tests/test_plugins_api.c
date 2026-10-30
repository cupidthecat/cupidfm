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
    ASSERT_EQ(after_count, before_count, "Partial pipe setup must not leak descriptors");
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

static bool test_exec_rejects_missing_working_directory(void) {
    PluginManager pm = {0};
    cs_vm *vm = cs_vm_new();
    ASSERT_NOT_NULL(vm, "VM should allocate");
    cs_value args[3] = {cs_str(vm, "printf"), cs_list(vm), cs_map(vm)};
    cs_value text = cs_str(vm, "must-not-run");
    cs_value cwd = cs_str(vm, "/dev/null/missing");
    cs_list_push(args[1], text);
    cs_map_set(args[2], "cwd", cwd);
    cs_value_release(text);
    cs_value_release(cwd);
    cs_value out = cs_nil();
    int result = nf_fm_exec(vm, &pm, 3, args, &out);
    cs_value code = cs_map_get(out, "code");
    cs_value output = cs_map_get(out, "stdout");
    bool ok = result == 0 && code.type == CS_T_INT && code.as.i == 126 && output.type == CS_T_STR &&
              !strcmp(cs_to_cstr(output), "");
    cs_value_release(code);
    cs_value_release(output);
    cs_value_release(out);
    for (size_t i = 0; i < 3; i++)
        cs_value_release(args[i]);
    cs_vm_free(vm);
    ASSERT_TRUE(ok, "Invalid working directory must prevent command execution");
    return true;
}

extern FileAttr mk_attr(const char *name, bool directory, ino_t inode);

static bool test_mime_information_and_entries_are_owned(void) {
    char directory[] = "/tmp/cupidfm-plugin-mime-XXXXXX";
    ASSERT_NOT_NULL(mkdtemp(directory), "Temporary directory should be created");
    char path[512];
    snprintf(path, sizeof(path), "%s/payload.txt", directory);
    FILE *file = fopen(path, "w");
    ASSERT_NOT_NULL(file, "Text fixture should open");
    ASSERT_TRUE(fputs("Plain text for plugin MIME regression.\n", file) >= 0,
                "Fixture should be written");
    fclose(file);
    cs_vm *vm = cs_vm_new();
    ASSERT_NOT_NULL(vm, "VM should allocate");
    PluginManager manager = {0};
    snprintf(manager.cwd, sizeof(manager.cwd), "%s", directory);
    cs_value argument = cs_str(vm, "payload.txt"), info = cs_nil(), entries = cs_nil();
    ASSERT_EQ(nf_fm_info(vm, &manager, 1, &argument, &info), 0, "File information should succeed");
    cs_value mime = cs_map_get(info, "mime");
    ASSERT_EQ(mime.type, CS_T_STR, "File information should contain a MIME string");
    ASSERT_STR_EQ(cs_to_cstr(mime), "text/plain",
                  "File MIME should survive returning from the native API");
    cs_value_release(mime);
    cs_value_release(info);
    cs_value_release(argument);
    Vector view = Vector_new(1);
    ASSERT_NOT_NULL(view.el, "View should allocate");
    view.el[0] = mk_attr("payload.txt", false, 0);
    ASSERT_NOT_NULL(view.el[0], "Entry should allocate");
    Vector_set_len_no_free(&view, 1);
    manager.view = &view;
    ASSERT_EQ(nf_fm_entries(vm, &manager, 0, NULL, &entries), 0, "Entry listing should succeed");
    cs_value entry = cs_list_get(entries, 0);
    mime = cs_map_get(entry, "mime");
    ASSERT_EQ(mime.type, CS_T_STR, "Entry should contain a MIME string");
    ASSERT_STR_EQ(cs_to_cstr(mime), "text/plain",
                  "Entry MIME must be copied before the temporary buffer expires");
    cs_value_release(mime);
    cs_value_release(entry);
    cs_value_release(entries);
    free_attr(view.el[0]);
    Vector_set_len_no_free(&view, 0);
    Vector_bye(&view);
    cs_vm_free(vm);
    mime_cache_clear();
    ASSERT_EQ(unlink(path), 0, "Fixture should be removed");
    ASSERT_EQ(rmdir(directory), 0, "Temporary directory should be removed");
    return true;
}

int main(void) {
    printf("=== Plugin API Tests ===\n\n");

    RUN_TEST(test_mime_information_and_entries_are_owned);
    RUN_TEST(test_clear_selected_paths_preserves_plugin_state);
    RUN_TEST(test_exec_closes_partial_pipe_failure);
    RUN_TEST(test_exec_reports_fork_failure_without_leaking);
    RUN_TEST(test_exec_rejects_missing_working_directory);

    PRINT_SUMMARY();
}
