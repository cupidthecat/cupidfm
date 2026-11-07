#ifndef TASKS_H
#define TASKS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    TASK_COPY,
    TASK_MOVE,
    TASK_TRASH,
    TASK_DELETE,
    TASK_LINK,
    TASK_RELATIVE_LINK,
    TASK_HARDLINK,
    TASK_SHELL,
    TASK_CREATE
} TaskKind;

typedef enum { TASK_QUEUED, TASK_RUNNING, TASK_DONE, TASK_FAILED, TASK_CANCELLED } TaskStatus;

typedef struct {
    unsigned id;
    TaskKind kind;
    TaskStatus status;
    size_t completed, total;
    uint64_t bytes;
    char message[4096];
} TaskInfo;

bool tasks_start(void);
bool tasks_submit(TaskKind kind, const char *const *paths, size_t count, const char *destination,
                  bool overwrite);
bool tasks_submit_shell(const char *command, const char *directory, const char *const *paths,
                        size_t count);
size_t tasks_snapshot(TaskInfo *out, size_t capacity);
void tasks_cancel(unsigned id);
size_t tasks_pending_count(void);
bool tasks_busy(void);
bool tasks_take_changed(void);
void tasks_stop(void);

#endif
