#include "memory.h"

#include "agent/llm_client.h"
#include "cJSON.h"
#include "config.h"
#include "message.h"
#include "util.h"

#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/* ── Memory layout ─────────────────────────────────────────────────
 *
 * .agent/memory/
 *   MEMORY.md          index only
 *   pattern.md         typed topic files, one bullet per memory
 *   preference.md
 *   architecture.md
 *   bug.md
 *   workflow.md
 *   fact.md
 */

typedef struct {
    MemoryType type;
    const char *name;
    const char *title;
    const char *description;
    const char *filename;
} MemoryTypeInfo;

static const MemoryTypeInfo TYPE_INFO[] = {
    {MEM_PATTERN, "pattern", "Patterns", "Coding patterns and conventions", "pattern.md"},
    {MEM_PREFERENCE, "preference", "Preferences", "User preferences and interaction style", "preference.md"},
    {MEM_ARCHITECTURE, "architecture", "Architecture", "Project architecture and module responsibilities", "architecture.md"},
    {MEM_BUG, "bug", "Bugs", "Known bugs, risks, and fixes", "bug.md"},
    {MEM_WORKFLOW, "workflow", "Workflows", "Build, test, run, and operational workflows", "workflow.md"},
    {MEM_FACT, "fact", "Facts", "General project facts", "fact.md"},
};

#define TYPE_COUNT ((int)(sizeof(TYPE_INFO) / sizeof(TYPE_INFO[0])))

static const MemoryTypeInfo *type_info(MemoryType type);

/* ── Path helpers ────────────────────────────────────────────────── */

static void agent_dir_path(char *buf, size_t cap) {
    snprintf(buf, cap, "%s/.agent", g_config.workdir);
}

static void memory_dir_path(char *buf, size_t cap) {
    snprintf(buf, cap, "%s/.agent/memory", g_config.workdir);
}

static void memory_index_path(char *buf, size_t cap) {
    snprintf(buf, cap, "%s/.agent/memory/MEMORY.md", g_config.workdir);
}

static void memory_type_path(MemoryType type, char *buf, size_t cap) {
    char dir[PATH_MAX];
    memory_dir_path(dir, sizeof(dir));
    snprintf(buf, cap, "%s/%s", dir, type_info(type)->filename);
}

static void legacy_memory_path(char *buf, size_t cap) {
    snprintf(buf, cap, "%s/.agent/memory.md", g_config.workdir);
}

static const MemoryTypeInfo *type_info(MemoryType type) {
    if (type >= 0 && type < TYPE_COUNT)
        return &TYPE_INFO[type];
    return &TYPE_INFO[MEM_FACT];
}

const char *memory_type_str(MemoryType type) {
    return type_info(type)->name;
}

static MemoryType parse_type_name(const char *name) {
    if (!name)
        return MEM_FACT;
    for (int i = 0; i < TYPE_COUNT; i++) {
        if (strcmp(name, TYPE_INFO[i].name) == 0)
            return TYPE_INFO[i].type;
    }
    return MEM_FACT;
}

/* ── Short-term observation buffer ───────────────────────────────── */

static MemoryObservation obs_buf[MEMORY_OBS_MAX];
static int obs_count;

static void obs_clear(void) {
    for (int i = 0; i < obs_count; i++)
        free(obs_buf[i].content);
    obs_count = 0;
}

static void obs_add(const char *content, MemoryType type) {
    if (obs_count >= MEMORY_OBS_MAX) {
        free(obs_buf[0].content);
        memmove(&obs_buf[0], &obs_buf[1],
                (MEMORY_OBS_MAX - 1) * sizeof(MemoryObservation));
        obs_count = MEMORY_OBS_MAX - 1;
    }
    obs_buf[obs_count].content = xstrdup(content);
    obs_buf[obs_count].type = type;
    obs_count++;
}

int memory_observation_count(void) {
    return obs_count;
}

/* ── Small file helpers ──────────────────────────────────────────── */

static int ensure_dir(const char *path) {
    if (mkdir(path, 0755) != 0 && errno != EEXIST)
        return -1;
    return 0;
}

static char *read_file_text(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0) {
        fclose(f);
        return xstrdup("");
    }
    char *buf = xmalloc((size_t)sz + 1);
    size_t n = fread(buf, 1, (size_t)sz, f);
    buf[n] = '\0';
    fclose(f);
    return buf;
}

static int write_file_text_atomic(const char *path, const char *content) {
    char tmp[PATH_MAX];
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);

    FILE *f = fopen(tmp, "w");
    if (!f)
        return -1;
    if (content && content[0])
        fwrite(content, 1, strlen(content), f);
    fflush(f);
    fclose(f);

    if (rename(tmp, path) != 0) {
        remove(tmp);
        return -1;
    }
    return 0;
}

static int append_str(char **buf, size_t *len, size_t *cap, const char *s) {
    size_t need = strlen(s);
    while (*len + need + 1 > *cap) {
        *cap *= 2;
        *buf = xrealloc(*buf, *cap);
    }
    memcpy(*buf + *len, s, need);
    *len += need;
    (*buf)[*len] = '\0';
    return 0;
}

static int append_fmt(char **buf, size_t *len, size_t *cap, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    va_list ap2;
    va_copy(ap2, ap);
    int needed = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (needed < 0) {
        va_end(ap2);
        return -1;
    }
    while (*len + (size_t)needed + 1 > *cap) {
        *cap *= 2;
        *buf = xrealloc(*buf, *cap);
    }
    vsnprintf(*buf + *len, *cap - *len, fmt, ap2);
    va_end(ap2);
    *len += (size_t)needed;
    return 0;
}

static char *trim_copy(const char *start, size_t len) {
    while (len > 0 && (*start == ' ' || *start == '\t')) {
        start++;
        len--;
    }
    while (len > 0 && (start[len - 1] == ' ' || start[len - 1] == '\t' ||
                       start[len - 1] == '\r' || start[len - 1] == '\n'))
        len--;
    char *out = xmalloc(len + 1);
    memcpy(out, start, len);
    out[len] = '\0';
    return out;
}

/* ── Typed memory files ──────────────────────────────────────────── */

static char *type_file_header(MemoryType type) {
    const MemoryTypeInfo *info = type_info(type);
    return xasprintf("---\n"
                     "name: %s\n"
                     "description: %s\n"
                     "type: %s\n"
                     "---\n\n",
                     info->title, info->description, info->name);
}

static int ensure_type_file(MemoryType type) {
    char path[PATH_MAX];
    memory_type_path(type, path, sizeof(path));

    FILE *f = fopen(path, "r");
    if (f) {
        fclose(f);
        return 0;
    }

    char *header = type_file_header(type);
    int rc = write_file_text_atomic(path, header);
    free(header);
    return rc;
}

static int write_index_file(void) {
    char path[PATH_MAX];
    memory_index_path(path, sizeof(path));

    size_t cap = 2048, len = 0;
    char *buf = xmalloc(cap);
    buf[0] = '\0';
    append_str(&buf, &len, &cap, "# Project Memory\n\n");
    for (int i = 0; i < TYPE_COUNT; i++) {
        append_fmt(&buf, &len, &cap, "- [%s](%s) - %s\n",
                   TYPE_INFO[i].title, TYPE_INFO[i].filename,
                   TYPE_INFO[i].description);
    }

    int rc = write_file_text_atomic(path, buf);
    free(buf);
    return rc;
}

static int read_entries_for_type(MemoryType type, char ***entries_out) {
    *entries_out = NULL;
    char path[PATH_MAX];
    memory_type_path(type, path, sizeof(path));

    char *text = read_file_text(path);
    if (!text)
        return 0;

    int cap = 16, count = 0;
    char **entries = xmalloc((size_t)cap * sizeof(char *));
    const char *p = text;
    while (*p) {
        const char *nl = strchr(p, '\n');
        size_t len = nl ? (size_t)(nl - p) : strlen(p);
        if (len >= 2 && p[0] == '-' && p[1] == ' ') {
            char *entry = trim_copy(p + 2, len - 2);
            if (entry[0]) {
                if (count >= cap) {
                    cap *= 2;
                    entries = xrealloc(entries, (size_t)cap * sizeof(char *));
                }
                entries[count++] = entry;
            } else {
                free(entry);
            }
        }
        p = nl ? nl + 1 : p + len;
    }
    free(text);

    if (count == 0) {
        free(entries);
        return 0;
    }
    *entries_out = entries;
    return count;
}

static void free_entries(char **entries, int count) {
    if (!entries)
        return;
    for (int i = 0; i < count; i++)
        free(entries[i]);
    free(entries);
}

static int write_entries_for_type(MemoryType type, char **entries, int count) {
    char path[PATH_MAX];
    memory_type_path(type, path, sizeof(path));

    size_t cap = 2048, len = 0;
    char *buf = type_file_header(type);
    len = strlen(buf);
    cap = len + 2048;
    buf = xrealloc(buf, cap);

    for (int i = 0; i < count; i++)
        append_fmt(&buf, &len, &cap, "- %s\n", entries[i]);

    int rc = write_file_text_atomic(path, buf);
    free(buf);
    return rc;
}

static int append_entry_unique(MemoryType type, const char *content) {
    char **entries = NULL;
    int count = read_entries_for_type(type, &entries);
    for (int i = 0; i < count; i++) {
        if (strcmp(entries[i], content) == 0) {
            free_entries(entries, count);
            return 0;
        }
    }

    entries = xrealloc(entries, (size_t)(count + 1) * sizeof(char *));
    entries[count++] = xstrdup(content);
    int rc = write_entries_for_type(type, entries, count);
    free_entries(entries, count);
    return rc;
}

static int parse_legacy_entry(const char *line, MemoryType *type_out, char **content_out) {
    if (strncmp(line, "- [", 3) != 0)
        return 0;
    const char *type_start = line + 3;
    const char *type_end = strchr(type_start, ']');
    if (!type_end || type_end[1] != ' ')
        return 0;

    char type_buf[32];
    size_t type_len = (size_t)(type_end - type_start);
    if (type_len >= sizeof(type_buf))
        type_len = sizeof(type_buf) - 1;
    memcpy(type_buf, type_start, type_len);
    type_buf[type_len] = '\0';

    *type_out = parse_type_name(type_buf);
    *content_out = trim_copy(type_end + 2, strlen(type_end + 2));
    return (*content_out)[0] != '\0';
}

static void migrate_legacy_memory(void) {
    char path[PATH_MAX];
    legacy_memory_path(path, sizeof(path));

    char *text = read_file_text(path);
    if (!text || !text[0]) {
        free(text);
        return;
    }

    const char *p = text;
    while (*p) {
        const char *nl = strchr(p, '\n');
        size_t len = nl ? (size_t)(nl - p) : strlen(p);
        char *line = trim_copy(p, len);
        MemoryType type;
        char *content = NULL;
        if (parse_legacy_entry(line, &type, &content))
            append_entry_unique(type, content);
        free(content);
        free(line);
        p = nl ? nl + 1 : p + len;
    }
    free(text);
}

/* ── Lifecycle ───────────────────────────────────────────────────── */

int memory_init(void) {
    char agent_dir[PATH_MAX];
    agent_dir_path(agent_dir, sizeof(agent_dir));
    if (ensure_dir(agent_dir) < 0)
        return -1;

    char memory_dir[PATH_MAX];
    memory_dir_path(memory_dir, sizeof(memory_dir));
    if (ensure_dir(memory_dir) < 0)
        return -1;

    if (write_index_file() < 0)
        return -1;

    for (int i = 0; i < TYPE_COUNT; i++) {
        if (ensure_type_file(TYPE_INFO[i].type) < 0)
            return -1;
    }

    migrate_legacy_memory();
    return 0;
}

void memory_shutdown(void) {
    obs_clear();
}

/* ── Remember / recall ───────────────────────────────────────────── */

int memory_remember(const char *content, MemoryType type) {
    if (!content || !content[0])
        return -1;
    return append_entry_unique(type, content);
}

char *memory_recall_filtered(const char *keyword, const char *type_str) {
    size_t cap = 4096, len = 0;
    char *buf = xmalloc(cap);
    buf[0] = '\0';

    int entry_idx = 0;
    bool any = false;
    MemoryType only_type = parse_type_name(type_str);
    bool filter_type = type_str && type_str[0];

    for (int t = 0; t < TYPE_COUNT; t++) {
        MemoryType type = TYPE_INFO[t].type;
        if (filter_type && type != only_type) {
            char **skip_entries = NULL;
            int skip_count = read_entries_for_type(type, &skip_entries);
            entry_idx += skip_count;
            free_entries(skip_entries, skip_count);
            continue;
        }

        char **entries = NULL;
        int count = read_entries_for_type(type, &entries);
        for (int i = 0; i < count; i++) {
            if (keyword && keyword[0] && !strstr(entries[i], keyword)) {
                entry_idx++;
                continue;
            }
            append_fmt(&buf, &len, &cap, "[%d] [%s] %s\n",
                       entry_idx, memory_type_str(type), entries[i]);
            any = true;
            entry_idx++;
        }
        free_entries(entries, count);
    }

    if (!any) {
        free(buf);
        return NULL;
    }
    return buf;
}

char *memory_recall(void) {
    return memory_recall_filtered(NULL, NULL);
}

/* ── Delete / update by global index ─────────────────────────────── */

static int find_index_location(int index, MemoryType *type_out, int *local_out) {
    if (index < 0)
        return -1;

    int entry_idx = 0;
    for (int t = 0; t < TYPE_COUNT; t++) {
        char **entries = NULL;
        int count = read_entries_for_type(TYPE_INFO[t].type, &entries);
        free_entries(entries, count);
        if (index < entry_idx + count) {
            *type_out = TYPE_INFO[t].type;
            *local_out = index - entry_idx;
            return 0;
        }
        entry_idx += count;
    }
    return -1;
}

int memory_delete(int index) {
    MemoryType type;
    int local;
    if (find_index_location(index, &type, &local) < 0)
        return -1;

    char **entries = NULL;
    int count = read_entries_for_type(type, &entries);
    if (local < 0 || local >= count) {
        free_entries(entries, count);
        return -1;
    }

    free(entries[local]);
    for (int i = local; i < count - 1; i++)
        entries[i] = entries[i + 1];
    count--;

    int rc = write_entries_for_type(type, entries, count);
    free_entries(entries, count);
    return rc;
}

int memory_update(int index, const char *content, MemoryType new_type) {
    if (!content || !content[0])
        return -1;

    MemoryType old_type;
    int local;
    if (find_index_location(index, &old_type, &local) < 0)
        return -1;

    char **entries = NULL;
    int count = read_entries_for_type(old_type, &entries);
    if (local < 0 || local >= count) {
        free_entries(entries, count);
        return -1;
    }

    if (old_type == new_type) {
        free(entries[local]);
        entries[local] = xstrdup(content);
        int rc = write_entries_for_type(old_type, entries, count);
        free_entries(entries, count);
        return rc;
    }

    free(entries[local]);
    for (int i = local; i < count - 1; i++)
        entries[i] = entries[i + 1];
    count--;
    int rc = write_entries_for_type(old_type, entries, count);
    free_entries(entries, count);
    if (rc < 0)
        return -1;
    return append_entry_unique(new_type, content);
}

/* ── Auto-capture from tool calls ────────────────────────────────── */

void memory_observe(const char *tool_name, const char *tool_args,
                    const char *tool_output) {
    (void)tool_output;
    if (!tool_name)
        return;

    MemoryType type = MEM_FACT;
    char content[1024];

    if (strcmp(tool_name, "write_file") == 0) {
        type = MEM_PATTERN;
        snprintf(content, sizeof(content), "wrote file %.200s", tool_args ? tool_args : "(unknown)");
    } else if (strcmp(tool_name, "edit_file") == 0) {
        type = MEM_PATTERN;
        snprintf(content, sizeof(content), "edited file %.200s", tool_args ? tool_args : "(unknown)");
    } else if (strcmp(tool_name, "bash") == 0) {
        type = MEM_WORKFLOW;
        snprintf(content, sizeof(content), "ran %.200s", tool_args ? tool_args : "(unknown)");
    } else if (strcmp(tool_name, "read_file") == 0) {
        type = MEM_FACT;
        snprintf(content, sizeof(content), "read file %.200s", tool_args ? tool_args : "(unknown)");
    } else {
        return;
    }

    obs_add(content, type);
}

/* ── Consolidation ───────────────────────────────────────────────── */

static char *observations_to_text(void) {
    size_t cap = 4096, len = 0;
    char *buf = xmalloc(cap);
    buf[0] = '\0';
    for (int i = 0; i < obs_count; i++) {
        append_fmt(&buf, &len, &cap, "- [%s] %s\n",
                   memory_type_str(obs_buf[i].type), obs_buf[i].content);
    }
    return buf;
}

static int remember_from_json(cJSON *root) {
    cJSON *items = cJSON_GetObjectItem(root, "memories");
    if (!items || !cJSON_IsArray(items))
        return 0;

    int saved = 0;
    int count = cJSON_GetArraySize(items);
    if (count > 5)
        count = 5;

    for (int i = 0; i < count; i++) {
        cJSON *item = cJSON_GetArrayItem(items, i);
        const char *type_str = cJSON_GetStringValue(cJSON_GetObjectItem(item, "type"));
        const char *content = cJSON_GetStringValue(cJSON_GetObjectItem(item, "content"));
        if (!content || !content[0])
            continue;
        MemoryType type = parse_type_name(type_str);
        if (memory_remember(content, type) == 0)
            saved++;
    }
    return saved;
}

static cJSON *parse_memory_json(const char *text) {
    if (!text)
        return NULL;

    cJSON *root = cJSON_Parse(text);
    if (root)
        return root;

    const char *start = strchr(text, '{');
    const char *end = strrchr(text, '}');
    if (!start || !end || end <= start)
        return NULL;

    size_t len = (size_t)(end - start + 1);
    char *slice = xmalloc(len + 1);
    memcpy(slice, start, len);
    slice[len] = '\0';
    root = cJSON_Parse(slice);
    free(slice);
    return root;
}

int memory_consolidate(char *err, size_t err_cap) {
    if (obs_count <= 0)
        return 0;

    char *obs = observations_to_text();
    char *existing = memory_recall();
    char *prompt = xasprintf(
        "Observed tool activity from this session:\n%s\n"
        "Existing project memory:\n%s\n\n"
        "Decide which observations are durable, useful project memories. "
        "Save only stable facts, user preferences, architecture decisions, build/test workflows, or real bugs/fixes. "
        "Do not save routine read_file/bash logs, transient command output, temporary failures, or vague facts. "
        "Return strict JSON only, with at most 5 items, in this exact shape:\n"
        "{\"memories\":[{\"type\":\"workflow|architecture|pattern|preference|bug|fact\",\"content\":\"concise memory\"}]}\n"
        "If nothing is worth saving, return {\"memories\":[]}.",
        obs, existing ? existing : "(none)");

    MessageList msgs;
    msg_list_init(&msgs);
    msg_list_push(&msgs, msg_user_json(prompt));

    const char *system =
        "You are a restricted memory consolidator for a coding agent. "
        "You do not call tools. You only return strict JSON. "
        "Be conservative: fewer high-value memories are better than noisy logs.";

    LLMResponse resp;
    memset(&resp, 0, sizeof(resp));
    int rc = llm_chat(&msgs, system, g_config.model, &resp, err, err_cap);
    msg_list_free(&msgs);
    free(prompt);
    free(obs);
    free(existing);

    if (rc != 0)
        return -1;

    if (resp.n_tool_calls > 0) {
        llm_response_free(&resp);
        snprintf(err, err_cap, "memory consolidator unexpectedly requested tools");
        return -1;
    }

    cJSON *root = parse_memory_json(resp.content);
    if (!root) {
        snprintf(err, err_cap, "memory consolidator returned invalid JSON");
        llm_response_free(&resp);
        return -1;
    }

    int saved = remember_from_json(root);
    cJSON_Delete(root);
    llm_response_free(&resp);
    obs_clear();
    return saved;
}

/* ── System prompt injection ─────────────────────────────────────── */

char *memory_build_prompt(void) {
    size_t cap = 4096, len = 0;
    char *buf = xmalloc(cap);
    buf[0] = '\0';

    const MemoryType prompt_types[] = {MEM_PREFERENCE, MEM_FACT};
    bool any = false;
    for (size_t i = 0; i < sizeof(prompt_types) / sizeof(prompt_types[0]); i++) {
        MemoryType type = prompt_types[i];
        char **entries = NULL;
        int count = read_entries_for_type(type, &entries);
        if (count > 0) {
            append_fmt(&buf, &len, &cap, "## %s\n", type_info(type)->title);
            for (int j = 0; j < count; j++)
                append_fmt(&buf, &len, &cap, "- %s\n", entries[j]);
            append_str(&buf, &len, &cap, "\n");
            any = true;
        }
        free_entries(entries, count);
    }

    if (!any) {
        free(buf);
        return xstrdup("");
    }
    return buf;
}
