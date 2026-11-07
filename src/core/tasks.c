#define _GNU_SOURCE
#include "tasks.h"
#include "process.h"

#include <errno.h>
#include <gio/gio.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define TASK_LIMIT 128

typedef struct {
    TaskInfo info;
    char **paths;
    char *destination;
    char *command;
    bool overwrite;
    GCancellable *cancel;
    GSubprocess *process;
} Task;

static Task *jobs[TASK_LIMIT];
static size_t job_count;
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t condition = PTHREAD_COND_INITIALIZER;
static pthread_t worker;
static bool started, stopping, changed;
static unsigned next_id = 1;

static void task_free(Task *task) {
    g_strfreev(task->paths);
    g_free(task->destination);
    g_free(task->command);
    g_clear_object(&task->cancel);
    g_free(task);
}

static void progress(goffset current, goffset total, gpointer data) {
    (void)total;
    Task *task = data;
    pthread_mutex_lock(&mutex);
    task->info.bytes = current < 0 ? 0 : (uint64_t)current;
    pthread_mutex_unlock(&mutex);
}

static bool remove_tree(GFile *file, GCancellable *cancel, unsigned depth, bool writable,
                        GError **error) {
    if (depth > 256) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_FAILED, "Directory nesting exceeds 256");
        return false;
    }
    GFileType type = g_file_query_file_type(file, G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, cancel);
    if (type == G_FILE_TYPE_DIRECTORY) {
        if (writable &&
            !g_file_set_attribute_uint32(file, G_FILE_ATTRIBUTE_UNIX_MODE, 0700,
                                         G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, cancel, error))
            return false;
        GFileEnumerator *entries =
            g_file_enumerate_children(file, G_FILE_ATTRIBUTE_STANDARD_NAME,
                                      G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, cancel, error);
        if (!entries)
            return false;
        GFileInfo *entry;
        bool ok = true;
        while ((entry = g_file_enumerator_next_file(entries, cancel, error))) {
            GFile *child = g_file_get_child(file, g_file_info_get_name(entry));
            ok = remove_tree(child, cancel, depth + 1, writable, error);
            g_object_unref(child);
            g_object_unref(entry);
            if (!ok)
                break;
        }
        if (*error)
            ok = false;
        g_object_unref(entries);
        if (!ok)
            return false;
    }
    return g_file_delete(file, cancel, error);
}

static bool copy_tree(Task *task, GFile *source, GFile *destination, unsigned depth,
                      GError **error) {
    if (depth > 256) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_FAILED, "Directory nesting exceeds 256");
        return false;
    }
    GFileCopyFlags flags = G_FILE_COPY_NOFOLLOW_SYMLINKS | G_FILE_COPY_ALL_METADATA;
    if (task->overwrite)
        flags |= G_FILE_COPY_OVERWRITE;
    GFileType type =
        g_file_query_file_type(source, G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, task->cancel);
    if (type != G_FILE_TYPE_DIRECTORY)
        return g_file_copy(source, destination, flags, task->cancel, progress, task, error);
    if (!g_file_make_directory(destination, task->cancel, error)) {
        if (!task->overwrite || !g_error_matches(*error, G_IO_ERROR, G_IO_ERROR_EXISTS) ||
            g_file_query_file_type(destination, G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
                                   task->cancel) != G_FILE_TYPE_DIRECTORY)
            return false;
        g_clear_error(error);
    }
    GFileEnumerator *entries =
        g_file_enumerate_children(source, G_FILE_ATTRIBUTE_STANDARD_NAME,
                                  G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, task->cancel, error);
    if (!entries)
        return false;
    bool ok = true;
    GFileInfo *entry;
    while ((entry = g_file_enumerator_next_file(entries, task->cancel, error))) {
        const char *name = g_file_info_get_name(entry);
        GFile *from = g_file_get_child(source, name), *to = g_file_get_child(destination, name);
        ok = copy_tree(task, from, to, depth + 1, error);
        g_object_unref(from);
        g_object_unref(to);
        g_object_unref(entry);
        if (!ok)
            break;
    }
    if (*error)
        ok = false;
    g_object_unref(entries);
    if (ok)
        (void)g_file_copy_attributes(source, destination, flags, task->cancel, NULL);
    return ok;
}

static char *relative_target(GFile *directory, GFile *source) {
    GFile *common = g_object_ref(directory);
    GString *prefix = g_string_new("");
    while (!g_file_equal(common, source) && !g_file_has_prefix(source, common)) {
        GFile *parent = g_file_get_parent(common);
        g_object_unref(common);
        common = parent;
        if (!common) {
            g_string_free(prefix, TRUE);
            return NULL;
        }
        g_string_append(prefix, "../");
    }
    char *tail = g_file_get_relative_path(common, source);
    g_string_append(prefix, tail && *tail ? tail : ".");
    g_free(tail);
    g_object_unref(common);
    return g_string_free(prefix, FALSE);
}

static bool make_directory(GFile *directory, GCancellable *cancel, GError **error) {
    if (g_file_make_directory_with_parents(directory, cancel, error))
        return true;
    if (g_error_matches(*error, G_IO_ERROR, G_IO_ERROR_EXISTS) &&
        g_file_query_file_type(directory, G_FILE_QUERY_INFO_NONE, cancel) ==
            G_FILE_TYPE_DIRECTORY) {
        g_clear_error(error);
        return true;
    }
    return false;
}

static bool create_path(Task *task, GFile *file, const char *path, GError **error) {
    if (g_str_has_suffix(path, "/"))
        return make_directory(file, task->cancel, error);
    GFile *parent = g_file_get_parent(file);
    bool ready = parent && make_directory(parent, task->cancel, error);
    g_clear_object(&parent);
    if (!ready)
        return false;
    GFileOutputStream *output = g_file_create(file, G_FILE_CREATE_NONE, task->cancel, error);
    if (!output)
        return false;
    bool ok = g_output_stream_close(G_OUTPUT_STREAM(output), task->cancel, error);
    g_object_unref(output);
    return ok;
}

static bool perform_file(Task *task, const char *path, GError **error) {
    GFile *source = g_file_new_for_path(path);
    GFile *directory = task->destination ? g_file_new_for_path(task->destination) : NULL;
    char *base = g_file_get_basename(source);
    GFile *destination = directory && base ? g_file_get_child(directory, base) : NULL;
    g_free(base);
    bool ok = false;
    if (task->info.kind == TASK_CREATE) {
        ok = create_path(task, source, path, error);
    } else if (task->info.kind == TASK_TRASH) {
        ok = g_file_trash(source, task->cancel, error);
    } else if (task->info.kind == TASK_DELETE) {
        ok = remove_tree(source, task->cancel, 0, false, error);
    } else if (destination) {
        /* Resolve the physical parent before creating anything. This also catches
         * a destination reached through a symlink into the source directory. */
        char *physical_source = realpath(path, NULL);
        char *physical_directory = realpath(task->destination, NULL);
        bool recursive = false;
        if (physical_source && physical_directory &&
            g_file_query_file_type(source, G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, task->cancel) ==
                G_FILE_TYPE_DIRECTORY) {
            size_t length = strlen(physical_source);
            recursive = !strncmp(physical_source, physical_directory, length) &&
                        (!physical_directory[length] || physical_directory[length] == '/');
        }
        free(physical_source);
        free(physical_directory);
        bool copying = task->info.kind == TASK_COPY || task->info.kind == TASK_MOVE;
        if (g_file_equal(source, destination) || (copying && recursive)) {
            g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                                "Destination is the source or inside the source directory");
        } else if (task->info.kind == TASK_COPY) {
            ok = copy_tree(task, source, destination, 0, error);
        } else if (task->info.kind == TASK_MOVE) {
            GFileCopyFlags flags =
                G_FILE_COPY_NOFOLLOW_SYMLINKS | (task->overwrite ? G_FILE_COPY_OVERWRITE : 0);
            ok = g_file_move(source, destination, flags, task->cancel, progress, task, error);
            if (!ok && g_error_matches(*error, G_IO_ERROR, G_IO_ERROR_WOULD_RECURSE)) {
                g_clear_error(error);
                ok = copy_tree(task, source, destination, 0, error) &&
                     remove_tree(source, task->cancel, 0, false, error);
            }
        } else if (task->info.kind == TASK_HARDLINK) {
            char *to = g_file_get_path(destination);
            ok = link(path, to) == 0;
            if (!ok)
                g_set_error(error, G_IO_ERROR, g_io_error_from_errno(errno), "%s",
                            g_strerror(errno));
            g_free(to);
        } else {
            char *target = task->info.kind == TASK_RELATIVE_LINK
                               ? relative_target(directory, source)
                               : g_strdup(path);
            if (target)
                ok = g_file_make_symbolic_link(destination, target, task->cancel, error);
            g_free(target);
        }
    }
    g_clear_object(&source);
    g_clear_object(&directory);
    g_clear_object(&destination);
    return ok;
}

static void shell_child_setup(gpointer unused) {
    (void)unused;
    (void)setpgid(0, 0);
}

static void stop_process(GSubprocess *process) {
    const char *identifier = g_subprocess_get_identifier(process);
    if (identifier) {
        char *end = NULL;
        long pid = strtol(identifier, &end, 10);
        if (end && !*end && pid > 1)
            (void)kill(-(pid_t)pid, SIGTERM);
    }
    g_subprocess_force_exit(process);
}

static bool perform_shell(Task *task, const char *extraction, GError **error) {
    size_t count = task->info.total;
    char **argv = g_new0(char *, count + 5);
    if (extraction) {
        // exec resolves this path before replacing the worker process image.
        g_free(argv);
        argv = g_new0(char *, 6);
        argv[0] = "/proc/self/exe";
        argv[1] = "--extract-worker";
        argv[2] = task->paths[0];
        argv[3] = "--into";
        argv[4] = (char *)extraction;
    } else {
        argv[0] = "sh";
        argv[1] = "-c";
        argv[2] = task->command;
        argv[3] = "cupidfm";
        for (size_t i = 0; i < count; i++)
            argv[i + 4] = task->paths[i];
    }
    FILE *log = tmpfile();
    if (!log) {
        g_set_error(error, G_IO_ERROR, g_io_error_from_errno(errno), "Cannot create task log: %s",
                    g_strerror(errno));
        g_free(argv);
        return false;
    }
    GSubprocessLauncher *launcher = g_subprocess_launcher_new(G_SUBPROCESS_FLAGS_STDIN_PIPE);
    if (!extraction)
        g_subprocess_launcher_set_cwd(launcher, task->destination);
    g_subprocess_launcher_set_child_setup(launcher, shell_child_setup, NULL, NULL);
    int output_fd = dup(fileno(log)), error_fd = dup(fileno(log));
    if (output_fd < 0 || error_fd < 0) {
        int saved = errno;
        if (output_fd >= 0)
            close(output_fd);
        if (error_fd >= 0)
            close(error_fd);
        g_object_unref(launcher);
        g_free(argv);
        fclose(log);
        g_set_error(error, G_IO_ERROR, g_io_error_from_errno(saved),
                    "Cannot redirect task output: %s", g_strerror(saved));
        return false;
    }
    g_subprocess_launcher_take_stdout_fd(launcher, output_fd);
    g_subprocess_launcher_take_stderr_fd(launcher, error_fd);
    GSubprocess *process = g_subprocess_launcher_spawnv(launcher, (const char *const *)argv, error);
    g_object_unref(launcher);
    g_free(argv);
    bool ok = false;
    if (process) {
        g_output_stream_close(g_subprocess_get_stdin_pipe(process), NULL, NULL);
        pthread_mutex_lock(&mutex);
        task->process = process;
        if (g_cancellable_is_cancelled(task->cancel))
            stop_process(process);
        pthread_mutex_unlock(&mutex);
        ok = g_subprocess_wait_check(process, task->cancel, error);
        pthread_mutex_lock(&mutex);
        task->process = NULL;
        pthread_mutex_unlock(&mutex);
        if (g_cancellable_is_cancelled(task->cancel)) {
            stop_process(process);
            (void)g_subprocess_wait(process, NULL, NULL);
        }
        g_object_unref(process);
    }
    (void)fseek(log, 0, SEEK_END);
    long length = ftell(log);
    (void)fseek(log, length > 3000 ? length - 3000 : 0, SEEK_SET);
    char output[3001] = {0};
    (void)fread(output, 1, sizeof(output) - 1, log);
    pthread_mutex_lock(&mutex);
    if (extraction)
        snprintf(task->info.message, sizeof(task->info.message), "Archive: %s\n%s", task->paths[0],
                 output);
    else
        snprintf(task->info.message, sizeof(task->info.message), "%s", output);
    pthread_mutex_unlock(&mutex);
    fclose(log);
    return ok;
}

static bool perform_extract(Task *task, GError **error) {
    char *parent = g_path_get_dirname(task->destination);
    char *temporary = g_build_filename(parent, ".cupidfm-extract-XXXXXX", NULL);
    g_free(parent);
    if (!g_mkdtemp(temporary)) {
        g_set_error(error, G_IO_ERROR, g_io_error_from_errno(errno), "Cannot stage extraction: %s",
                    g_strerror(errno));
        g_free(temporary);
        return false;
    }
    char *content = g_build_filename(temporary, "content", NULL);
    bool ok = perform_shell(task, content, error);
    if (ok && g_cancellable_set_error_if_cancelled(task->cancel, error))
        ok = false;
    char *published = NULL;
    if (ok) {
        for (unsigned suffix = 0; suffix < 10000; suffix++) {
            g_free(published);
            published = suffix ? g_strdup_printf("%s_%u", task->destination, suffix)
                               : g_strdup(task->destination);
            if (file_command_rename(content, published))
                break;
            if (errno != EEXIST || suffix == 9999) {
                g_set_error(error, G_IO_ERROR, g_io_error_from_errno(errno),
                            "Cannot place extracted files: %s", g_strerror(errno));
                ok = false;
                break;
            }
        }
    }
    GFile *staging = g_file_new_for_path(temporary);
    GError *cleanup = NULL;
    if (!remove_tree(staging, NULL, 0, true, &cleanup)) {
        if (!*error)
            g_propagate_error(error, cleanup);
        else
            g_clear_error(&cleanup);
        ok = false;
    }
    g_object_unref(staging);
    if (ok) {
        pthread_mutex_lock(&mutex);
        snprintf(task->info.message, sizeof(task->info.message), "Extracted to %s", published);
        pthread_mutex_unlock(&mutex);
    }
    g_free(published);
    g_free(content);
    g_free(temporary);
    return ok;
}

static void *run_worker(void *unused) {
    (void)unused;
    pthread_mutex_lock(&mutex);
    for (;;) {
        Task *task = NULL;
        for (size_t i = 0; i < job_count; i++)
            if (jobs[i]->info.status == TASK_QUEUED) {
                task = jobs[i];
                break;
            }
        if (!task) {
            if (stopping)
                break;
            pthread_cond_wait(&condition, &mutex);
            continue;
        }
        task->info.status = TASK_RUNNING;
        pthread_mutex_unlock(&mutex);
        GError *error = NULL;
        bool ok = true;
        if (g_cancellable_set_error_if_cancelled(task->cancel, &error)) {
            ok = false;
        } else if (task->info.kind == TASK_SHELL) {
            ok = perform_shell(task, NULL, &error);
        } else if (task->info.kind == TASK_EXTRACT) {
            ok = perform_extract(task, &error);
        } else {
            for (size_t i = 0; i < task->info.total; i++) {
                if (g_cancellable_set_error_if_cancelled(task->cancel, &error)) {
                    ok = false;
                    break;
                }
                if (!perform_file(task, task->paths[i], &error)) {
                    ok = false;
                    if (task->info.kind != TASK_CREATE)
                        break;
                    pthread_mutex_lock(&mutex);
                    size_t used = strlen(task->info.message);
                    snprintf(task->info.message + used, sizeof(task->info.message) - used,
                             "%s: %s\n", task->paths[i], error ? error->message : "Create failed");
                    pthread_mutex_unlock(&mutex);
                    g_clear_error(&error);
                    continue;
                }
                pthread_mutex_lock(&mutex);
                task->info.completed++;
                pthread_mutex_unlock(&mutex);
            }
        }
        pthread_mutex_lock(&mutex);
        task->info.status = g_cancellable_is_cancelled(task->cancel) ? TASK_CANCELLED
                            : ok                                     ? TASK_DONE
                                                                     : TASK_FAILED;
        if (error) {
            size_t used = (task->info.kind == TASK_SHELL || task->info.kind == TASK_EXTRACT)
                              ? strlen(task->info.message)
                              : 0;
            snprintf(task->info.message + used, sizeof(task->info.message) - used, "%s%s",
                     used ? "\n" : "", error->message);
        } else if (ok && (task->info.kind == TASK_SHELL || task->info.kind == TASK_EXTRACT))
            task->info.completed = task->info.total;
        g_clear_error(&error);
        changed = true;
    }
    pthread_mutex_unlock(&mutex);
    return NULL;
}

bool tasks_start(void) {
    if (started)
        return true;
    stopping = false;
    started = pthread_create(&worker, NULL, run_worker, NULL) == 0;
    return started;
}

static bool submit(TaskKind kind, const char *const *paths, size_t count, const char *destination,
                   bool overwrite, const char *command) {
    if (!started || kind < TASK_COPY || kind > TASK_EXTRACT || count > 65536 ||
        (!count && kind != TASK_SHELL) || (count && !paths) ||
        ((kind != TASK_TRASH && kind != TASK_DELETE && kind != TASK_CREATE) &&
         (!destination || !*destination)))
        return false;
    if (kind == TASK_EXTRACT && (count != 1 || destination[0] != '/'))
        return false;
    for (size_t i = 0; i < count; i++)
        if (!paths[i] || paths[i][0] != '/')
            return false;
    Task *task = g_new0(Task, 1);
    task->paths = g_new0(char *, count + 1);
    for (size_t i = 0; i < count; i++)
        task->paths[i] = g_strdup(paths[i]);
    task->destination = g_strdup(destination);
    task->command = g_strdup(command);
    task->overwrite = overwrite;
    task->cancel = g_cancellable_new();
    task->info.kind = kind;
    task->info.total = count;
    if (kind == TASK_EXTRACT)
        snprintf(task->info.message, sizeof(task->info.message), "Archive: %s", paths[0]);
    pthread_mutex_lock(&mutex);
    if (job_count == TASK_LIMIT) {
        for (size_t i = 0; i < job_count; i++) {
            if (jobs[i]->info.status > TASK_RUNNING) {
                task_free(jobs[i]);
                memmove(jobs + i, jobs + i + 1, (job_count - i - 1) * sizeof(*jobs));
                job_count--;
                break;
            }
        }
    }
    if (job_count == TASK_LIMIT || stopping) {
        pthread_mutex_unlock(&mutex);
        task_free(task);
        return false;
    }
    task->info.id = next_id++;
    jobs[job_count++] = task;
    pthread_cond_signal(&condition);
    pthread_mutex_unlock(&mutex);
    return true;
}

bool tasks_submit(TaskKind kind, const char *const *paths, size_t count, const char *destination,
                  bool overwrite) {
    return kind != TASK_SHELL && submit(kind, paths, count, destination, overwrite, NULL);
}

bool tasks_submit_shell(const char *command, const char *directory, const char *const *paths,
                        size_t count) {
    return command && directory && submit(TASK_SHELL, paths, count, directory, false, command);
}

size_t tasks_snapshot(TaskInfo *out, size_t capacity) {
    pthread_mutex_lock(&mutex);
    size_t count = job_count < capacity ? job_count : capacity;
    for (size_t i = 0; i < count; i++)
        out[i] = jobs[i]->info;
    pthread_mutex_unlock(&mutex);
    return count;
}

void tasks_cancel(unsigned id) {
    pthread_mutex_lock(&mutex);
    for (size_t i = 0; i < job_count; i++) {
        Task *task = jobs[i];
        if (task->info.id != id || task->info.status > TASK_RUNNING)
            continue;
        g_cancellable_cancel(task->cancel);
        if (task->process)
            stop_process(task->process);
    }
    pthread_mutex_unlock(&mutex);
}

size_t tasks_pending_count(void) {
    size_t busy = 0;
    pthread_mutex_lock(&mutex);
    for (size_t i = 0; i < job_count; i++)
        busy += jobs[i]->info.status <= TASK_RUNNING;
    pthread_mutex_unlock(&mutex);
    return busy;
}

bool tasks_busy(void) {
    return tasks_pending_count() != 0;
}

bool tasks_take_changed(void) {
    pthread_mutex_lock(&mutex);
    bool result = changed;
    changed = false;
    pthread_mutex_unlock(&mutex);
    return result;
}

void tasks_stop(void) {
    if (!started)
        return;
    pthread_mutex_lock(&mutex);
    stopping = true;
    for (size_t i = 0; i < job_count; i++) {
        g_cancellable_cancel(jobs[i]->cancel);
        if (jobs[i]->process)
            stop_process(jobs[i]->process);
    }
    pthread_cond_signal(&condition);
    pthread_mutex_unlock(&mutex);
    pthread_join(worker, NULL);
    for (size_t i = 0; i < job_count; i++)
        task_free(jobs[i]);
    job_count = 0;
    started = stopping = changed = false;
}
