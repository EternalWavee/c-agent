/*
 * subagent tool — background child agents with spawn/status/wait.
 *
 * Three tools:
 *   subagent_spawn  — start a child agent in a background thread, return ID
 *   subagent_status — list all subagents and their states
 *   subagent_wait   — block until a specific subagent finishes, return result
 */
#include "tools/tools.h"
#include "agent/agent.h"
#include "config.h"
#include "util.h"

#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define MAX_SUBAGENTS 8

typedef enum { SUB_RUNNING, SUB_DONE, SUB_FAILED } SubState;

typedef struct {
    int id;
    int active;
    char *task;
    SubState state;
    char *result;
    pthread_t thread;
} SubSlot;

static SubSlot pool[MAX_SUBAGENTS];
static pthread_mutex_t pool_lock = PTHREAD_MUTEX_INITIALIZER;
static int next_id = 1;

/* ── Worker thread ────────────────────────────────────────────────── */

static void *subagent_worker(void *arg) {
    SubSlot *slot = (SubSlot *)arg;

    fprintf(stderr, "[subagent_%d] starting: %s\n", slot->id, slot->task);

    Agent *child = agent_create_silent();
    if (!child) {
        fprintf(stderr, "[subagent_%d] agent_create failed\n", slot->id);
        pthread_mutex_lock(&pool_lock);
        slot->state = SUB_FAILED;
        slot->result = xstrdup("failed to create subagent");
        pthread_mutex_unlock(&pool_lock);
        return NULL;
    }

    const char *reply = agent_chat(child, slot->task);
    fprintf(stderr, "[subagent_%d] agent_chat returned: %s\n",
            slot->id, reply ? reply : "(null)");

    pthread_mutex_lock(&pool_lock);
    if (reply && reply[0]) {
        slot->state = SUB_DONE;
        slot->result = xstrdup(reply);
    } else {
        slot->state = SUB_DONE;
        slot->result = xstrdup("(subagent returned no output)");
    }
    pthread_mutex_unlock(&pool_lock);

    agent_free(child);
    return NULL;
}

/* ── subagent_spawn ──────────────────────────────────────────────── */

static ToolResult tool_spawn(cJSON *args) {
    const char *task = cJSON_GetStringValue(cJSON_GetObjectItem(args, "task"));
    if (!task || !task[0])
        return (ToolResult){.ok = false,
                            .output = xstrdup("missing 'task' argument")};

    pthread_mutex_lock(&pool_lock);

    int slot_idx = -1;
    for (int i = 0; i < MAX_SUBAGENTS; i++) {
        if (!pool[i].active) { slot_idx = i; break; }
    }
    if (slot_idx < 0) {
        pthread_mutex_unlock(&pool_lock);
        return (ToolResult){.ok = false,
                            .output = xstrdup("subagent pool full (max 8)")};
    }

    SubSlot *slot = &pool[slot_idx];
    slot->id = next_id++;
    slot->active = 1;
    slot->task = xstrdup(task);
    slot->state = SUB_RUNNING;
    slot->result = NULL;

    pthread_create(&slot->thread, NULL, subagent_worker, slot);

    int id = slot->id;
    pthread_mutex_unlock(&pool_lock);

    return (ToolResult){.ok = true,
                        .output = xasprintf("subagent_%d started: %s", id, task)};
}

/* ── subagent_status ─────────────────────────────────────────────── */

static ToolResult tool_status(cJSON *args) {
    (void)args;

    pthread_mutex_lock(&pool_lock);

    int any = 0;
    size_t cap = 1024;
    size_t len = 0;
    char *buf = xmalloc(cap);
    buf[0] = '\0';

    for (int i = 0; i < MAX_SUBAGENTS; i++) {
        if (!pool[i].active) continue;
        any = 1;

        const char *state_str;
        switch (pool[i].state) {
            case SUB_RUNNING: state_str = "running"; break;
            case SUB_DONE:    state_str = "done"; break;
            case SUB_FAILED:  state_str = "failed"; break;
            default:          state_str = "unknown"; break;
        }

        char line[512];
        snprintf(line, sizeof(line), "subagent_%d [%s]: %s\n",
                 pool[i].id, state_str, pool[i].task ? pool[i].task : "");

        size_t line_len = strlen(line);
        while (len + line_len + 1 > cap) { cap *= 2; buf = xrealloc(buf, cap); }
        memcpy(buf + len, line, line_len);
        len += line_len;
        buf[len] = '\0';
    }

    pthread_mutex_unlock(&pool_lock);

    if (!any) {
        free(buf);
        return (ToolResult){.ok = true, .output = xstrdup("no active subagents")};
    }

    return (ToolResult){.ok = true, .output = buf};
}

/* ── subagent_wait ───────────────────────────────────────────────── */

static ToolResult tool_wait(cJSON *args) {
    cJSON *id_obj = cJSON_GetObjectItem(args, "id");

    pthread_mutex_lock(&pool_lock);

    /* If no id specified, wait for the first running one */
    int slot_idx = -1;
    if (id_obj && cJSON_IsNumber(id_obj)) {
        int target_id = (int)cJSON_GetNumberValue(id_obj);
        for (int i = 0; i < MAX_SUBAGENTS; i++) {
            if (pool[i].active && pool[i].id == target_id) {
                slot_idx = i;
                break;
            }
        }
    } else {
        for (int i = 0; i < MAX_SUBAGENTS; i++) {
            if (pool[i].active && pool[i].state == SUB_RUNNING) {
                slot_idx = i;
                break;
            }
        }
    }

    if (slot_idx < 0) {
        pthread_mutex_unlock(&pool_lock);
        return (ToolResult){.ok = false,
                            .output = xstrdup("no matching subagent found")};
    }

    SubSlot *slot = &pool[slot_idx];
    int id = slot->id;

    /* If already done, return result immediately */
    if (slot->state != SUB_RUNNING) {
        char *result = slot->result ? xstrdup(slot->result) : xstrdup("(no output)");
        free(slot->task);
        free(slot->result);
        slot->active = 0;
        pthread_mutex_unlock(&pool_lock);
        return (ToolResult){.ok = true,
                            .output = xasprintf("subagent_%d result:\n%s", id, result)};
    }

    /* Need to join — unlock first so the thread can finish */
    pthread_t tid = slot->thread;
    pthread_mutex_unlock(&pool_lock);

    pthread_join(tid, NULL);

    /* Re-lock to read result */
    pthread_mutex_lock(&pool_lock);
    char *result = slot->result ? xstrdup(slot->result) : xstrdup("(no output)");
    free(slot->task);
    free(slot->result);
    slot->active = 0;
    pthread_mutex_unlock(&pool_lock);

    return (ToolResult){.ok = true,
                        .output = xasprintf("subagent_%d result:\n%s", id, result)};
}

/* ── Cleanup (called on exit) ────────────────────────────────────── */

void subagent_cleanup(void) {
    pthread_mutex_lock(&pool_lock);
    for (int i = 0; i < MAX_SUBAGENTS; i++) {
        if (pool[i].active && pool[i].state == SUB_RUNNING) {
            pthread_t tid = pool[i].thread;
            pthread_mutex_unlock(&pool_lock);
            pthread_join(tid, NULL);
            pthread_mutex_lock(&pool_lock);
        }
        if (pool[i].active) {
            free(pool[i].task);
            free(pool[i].result);
            pool[i].active = 0;
        }
    }
    pthread_mutex_unlock(&pool_lock);
}

/* ── Tool definitions ────────────────────────────────────────────── */

ToolDef subagent_spawn_def = {
    .name = "subagent_spawn",
    .desc = "Spawn a child agent in the background for an independent subtask. "
            "Returns immediately with an ID. Use subagent_wait to get the result.",
    .param_schema =
        "{\"type\":\"object\","
        "\"properties\":{"
        "\"task\":{\"type\":\"string\","
        "\"description\":\"The subtask for the child agent to complete\"}"
        "},\"required\":[\"task\"]}",
    .exec = tool_spawn,
    .read_only = false,
};

ToolDef subagent_status_def = {
    .name = "subagent_status",
    .desc = "List all subagents and their current status (running/done/failed).",
    .param_schema =
        "{\"type\":\"object\","
        "\"properties\":{},"
        "\"required\":[]}",
    .exec = tool_status,
    .read_only = true,
};

ToolDef subagent_wait_def = {
    .name = "subagent_wait",
    .desc = "Wait for a subagent to finish and return its result. "
            "If no id is specified, waits for the first running one.",
    .param_schema =
        "{\"type\":\"object\","
        "\"properties\":{"
        "\"id\":{\"type\":\"number\","
        "\"description\":\"The subagent ID to wait for (optional)\"}"
        "},\"required\":[]}",
    .exec = tool_wait,
    .read_only = false,
};
