#define _GNU_SOURCE
#include "tasks.h"

#include <assert.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static TaskInfo wait_for_task(size_t index, TaskStatus status) {
    TaskInfo tasks[128];
    for (int tries = 0; tries < 500; tries++) {
        size_t count = tasks_snapshot(tasks, 128);
        if (index < count && tasks[index].status > TASK_RUNNING) {
            if (tasks[index].status != status)
                fprintf(stderr, "Task %u: status %d: %s\n", tasks[index].id, tasks[index].status,
                        tasks[index].message);
            assert(tasks[index].status == status);
            return tasks[index];
        }
        struct timespec delay = {0, 10000000};
        nanosleep(&delay, NULL);
    }
    assert(!"task timed out");
    return (TaskInfo){0};
}

static void write_text(const char *path, const char *text) {
    FILE *file = fopen(path, "w");
    assert(file);
    assert(fputs(text, file) >= 0);
    assert(!fclose(file));
}

static void assert_text(const char *path, const char *expected) {
    FILE *file = fopen(path, "r");
    assert(file);
    char text[256] = {0};
    (void)fread(text, 1, sizeof(text) - 1, file);
    fclose(file);
    assert(!strcmp(text, expected));
}

static void clean_tree(const char *path) {
    struct stat info;
    assert(!lstat(path, &info));
    if (S_ISDIR(info.st_mode)) {
        DIR *directory = opendir(path);
        assert(directory);
        struct dirent *entry;
        while ((entry = readdir(directory))) {
            if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, ".."))
                continue;
            char child[4096];
            snprintf(child, sizeof(child), "%s/%s", path, entry->d_name);
            clean_tree(child);
        }
        closedir(directory);
        assert(!rmdir(path));
    } else {
        assert(!unlink(path));
    }
}

int main(void) {
    char root[] = "/tmp/cupidfm-tasks-XXXXXX";
    assert(mkdtemp(root));
    char source[4096], dest[4096], copied[4096], data[4096], nested[4096], link_path[4096];
    snprintf(source, sizeof(source), "%s/literal$(touch injected)", root);
    snprintf(dest, sizeof(dest), "%s/destination", root);
    snprintf(data, sizeof(data), "%s/data", root);
    assert(!mkdir(dest, 0700) && !mkdir(data, 0700));
    assert(!setenv("XDG_DATA_HOME", data, 1));
    write_text(source, "valuable data");
    const char *paths[] = {source};
    assert(tasks_start());
    assert(!tasks_submit(TASK_COPY, NULL, 1, dest, false));
    assert(tasks_submit(TASK_COPY, paths, 1, dest, false));
    wait_for_task(0, TASK_DONE);
    snprintf(copied, sizeof(copied), "%s/literal$(touch injected)", dest);
    assert_text(copied, "valuable data");
    assert_text(source, "valuable data");
    assert(tasks_take_changed());
    assert(!tasks_take_changed());

    /* Destination collisions preserve existing contents unless force was explicit. */
    write_text(copied, "keep destination");
    assert(tasks_submit(TASK_COPY, paths, 1, dest, false));
    wait_for_task(1, TASK_FAILED);
    assert_text(copied, "keep destination");
    assert(tasks_submit(TASK_COPY, paths, 1, dest, true));
    wait_for_task(2, TASK_DONE);
    assert_text(copied, "valuable data");

    snprintf(nested, sizeof(nested), "%s/tree", root);
    assert(!mkdir(nested, 0700));
    snprintf(link_path, sizeof(link_path), "%s/broken", nested);
    assert(!symlink("../absent", link_path));
    const char *tree[] = {nested};
    assert(tasks_submit(TASK_COPY, tree, 1, dest, false));
    wait_for_task(3, TASK_DONE);
    char target[4096], value[64];
    snprintf(target, sizeof(target), "%s/tree/broken", dest);
    ssize_t length = readlink(target, value, sizeof(value) - 1);
    assert(length > 0);
    value[length] = '\0';
    assert(!strcmp(value, "../absent"));
    /* Recursing into the source, including through a symlink, must fail before creation. */
    assert(tasks_submit(TASK_COPY, tree, 1, nested, false));
    wait_for_task(4, TASK_FAILED);
    snprintf(target, sizeof(target), "%s/inside-alias", root);
    assert(!symlink(nested, target));
    assert(tasks_submit(TASK_COPY, tree, 1, target, false));
    wait_for_task(5, TASK_FAILED);

    /* Shell arguments remain literal and the background command runs in its tab's directory. */
    assert(tasks_submit_shell("printf '%s' \"$1\" > receipt", dest, paths, 1));
    wait_for_task(6, TASK_DONE);
    snprintf(target, sizeof(target), "%s/receipt", dest);
    assert_text(target, source);
    snprintf(target, sizeof(target), "%s/injected", dest);
    assert(access(target, F_OK));

    assert(tasks_submit_shell("sleep 30", dest, NULL, 0));
    TaskInfo snapshot[128];
    for (int i = 0; i < 100; i++) {
        assert(tasks_snapshot(snapshot, 128) >= 8);
        if (snapshot[7].status == TASK_RUNNING)
            break;
        struct timespec delay = {0, 10000000};
        nanosleep(&delay, NULL);
    }
    assert(tasks_submit(TASK_COPY, paths, 1, dest, false));
    assert(tasks_snapshot(snapshot, 128) == 9);
    tasks_cancel(snapshot[8].id);
    tasks_cancel(snapshot[7].id);
    wait_for_task(7, TASK_CANCELLED);
    wait_for_task(8, TASK_CANCELLED);

    const char *copied_paths[] = {copied};
    assert(tasks_submit(TASK_TRASH, copied_paths, 1, NULL, false));
    wait_for_task(9, TASK_DONE);
    assert(access(copied, F_OK));
    assert(tasks_submit(TASK_MOVE, paths, 1, dest, false));
    wait_for_task(10, TASK_DONE);
    assert(access(source, F_OK));
    assert_text(copied, "valuable data");
    const TaskKind links[] = {TASK_LINK, TASK_RELATIVE_LINK, TASK_HARDLINK};
    for (size_t i = 0; i < 3; i++) {
        char link_directory[4096], linked[4096];
        snprintf(link_directory, sizeof(link_directory), "%s/links-%zu", root, i);
        assert(!mkdir(link_directory, 0700));
        assert(tasks_submit(links[i], copied_paths, 1, link_directory, false));
        wait_for_task(11 + i, TASK_DONE);
        snprintf(linked, sizeof(linked), "%s/literal$(touch injected)", link_directory);
        assert_text(linked, "valuable data");
        struct stat original, linked_info;
        assert(!stat(copied, &original) && !lstat(linked, &linked_info));
        if (links[i] == TASK_HARDLINK)
            assert(original.st_ino == linked_info.st_ino);
        else {
            assert(S_ISLNK(linked_info.st_mode));
            ssize_t size = readlink(linked, value, sizeof(value) - 1);
            assert(size > 0);
            assert((value[0] == '/') == (links[i] == TASK_LINK));
        }
    }
    snprintf(target, sizeof(target), "%s/links-0/literal$(touch injected)", root);
    const char *unlink_paths[] = {target};
    assert(tasks_submit(TASK_DELETE, unlink_paths, 1, NULL, false));
    wait_for_task(14, TASK_DONE);
    assert_text(copied, "valuable data");
    assert(tasks_submit(TASK_DELETE, tree, 1, NULL, false));
    wait_for_task(15, TASK_DONE);
    assert(access(nested, F_OK));

    /* Cancelling a queued shell must not start its command. */
    assert(tasks_submit_shell("sleep 30", dest, NULL, 0));
    assert(tasks_submit_shell("touch cancelled-should-not-run", dest, NULL, 0));
    assert(tasks_snapshot(snapshot, 128) == 18);
    tasks_cancel(snapshot[17].id);
    tasks_cancel(snapshot[16].id);
    wait_for_task(16, TASK_CANCELLED);
    wait_for_task(17, TASK_CANCELLED);
    snprintf(target, sizeof(target), "%s/cancelled-should-not-run", dest);
    assert(access(target, F_OK));
    /* Linking a directory inside itself is legal and must not traverse the link. */
    snprintf(nested, sizeof(nested), "%s/link-directory", root);
    assert(!mkdir(nested, 0700));
    const char *directory_link[] = {nested};
    assert(tasks_submit(TASK_RELATIVE_LINK, directory_link, 1, nested, false));
    wait_for_task(18, TASK_DONE);
    snprintf(target, sizeof(target), "%s/link-directory", nested);
    ssize_t linked_size = readlink(target, value, sizeof(value) - 1);
    assert(linked_size == 1 && value[0] == '.');
    char new_file[4096], new_directory[4096], after_collision[4096], cancelled_create[4096];
    snprintf(new_file, sizeof(new_file), "%s/created/nested/file.txt", root);
    snprintf(new_directory, sizeof(new_directory), "%s/created/empty/", root);
    snprintf(after_collision, sizeof(after_collision), "%s/created/after-collision.txt", root);
    const char *created_paths[] = {new_file, new_directory, copied, after_collision};
    assert(tasks_submit(TASK_CREATE, created_paths, 4, NULL, false));
    TaskInfo created = wait_for_task(19, TASK_FAILED);
    assert(created.completed == 3 && created.total == 4);
    assert(strstr(created.message, copied));
    assert_text(new_file, "");
    assert_text(after_collision, "");
    assert_text(copied, "valuable data");
    struct stat created_info;
    assert(!stat(new_directory, &created_info) && S_ISDIR(created_info.st_mode));
    snprintf(cancelled_create, sizeof(cancelled_create), "%s/cancelled-create", root);
    const char *cancelled_path[] = {cancelled_create};
    assert(tasks_submit_shell("sleep 30", dest, NULL, 0));
    assert(tasks_submit(TASK_CREATE, cancelled_path, 1, NULL, false));
    assert(tasks_snapshot(snapshot, 128) == 22);
    tasks_cancel(snapshot[21].id);
    tasks_cancel(snapshot[20].id);
    wait_for_task(20, TASK_CANCELLED);
    wait_for_task(21, TASK_CANCELLED);
    assert(access(cancelled_create, F_OK));
    tasks_stop();
    /* Trashing never uses CupidFM's session-temporary cleanup path. */
    snprintf(target, sizeof(target), "%s/Trash/files/literal$(touch injected)", data);
    assert_text(target, "valuable data");
    clean_tree(root);
    puts("Background copy, move, trash, no-follow traversal, overwrite, shell, and cancellation "
         "passed");
    return 0;
}
