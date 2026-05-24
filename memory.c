#include "memory.h"

#include "agent/llm_client.h"
#include "config.h"
#include "message.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/* ── Path helper ─────────────────────────────────────────────────── */

static void memory_path(char *buf, size_t cap) {
    snprintf(buf, cap, "%s/.agent/memory.md", g_config.workdir);
}

/* ── Type names ──────────────────────────────────────────────────── */

static const char *TYPE_NAMES[] = {
    "pattern", "preference", "architecture", "bug", "workflow", "fact",
};

const char *memory_type_str(MemoryType type) {
    if (type >= 0 && type <= MEM_FACT)
        return TYPE_NAMES[type];
    return "fact";
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
        /* Ring buffer: drop oldest */
        free(obs_buf[0].content);
        memmove(&obs_buf[0], &obs_buf[1],
                (MEMORY_OBS_MAX - 1) * sizeof(MemoryObservation));
        obs_count = MEMORY_OBS_MAX - 1;
    }
    obs_buf[obs_count].content = xstrdup(content);
    obs_buf[obs_count].type = type;
    obs_count++;
}

/* ── Lifecycle ───────────────────────────────────────────────────── */

int memory_init(void) {
    char agent_dir[PATH_MAX];
    snprintf(agent_dir, sizeof(agent_dir), "%s/.agent", g_config.workdir);
    mkdir(agent_dir, 0755);

    char path[PATH_MAX];
    memory_path(path, sizeof(path));

    FILE *f = fopen(path, "r");
    if (f) { fclose(f); return 0; }

    f = fopen(path, "w");
    if (!f) return -1;
    fprintf(f, "# Project Memory\n\n");
    fclose(f);
    return 0;
}

void memory_shutdown(void) {
    obs_clear();
}

/* ── Remember (write to long-term) ───────────────────────────────── */

int memory_remember(const char *content, MemoryType type) {
    if (!content || !content[0]) return -1;

    char path[PATH_MAX];
    memory_path(path, sizeof(path));

    FILE *f = fopen(path, "a");
    if (!f) return -1;

    fprintf(f, "- [%s] %s\n", memory_type_str(type), content);
    fflush(f);
    fclose(f);
    return 0;
}

/* ── Recall (read long-term) ─────────────────────────────────────── */

char *memory_recall(void) {
    char path[PATH_MAX];
    memory_path(path, sizeof(path));

    FILE *f = fopen(path, "r");
    if (!f) return NULL;

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0) { fclose(f); return NULL; }

    char *buf = xmalloc((size_t)sz + 1);
    fread(buf, 1, (size_t)sz, f);
    buf[sz] = '\0';
    fclose(f);
    return buf;
}

/* ── Auto-capture from tool calls ────────────────────────────────── */

void memory_observe(const char *tool_name, const char *tool_args,
                    const char *tool_output) {
    (void)tool_output;
    if (!tool_name) return;

    MemoryType type = MEM_FACT;
    char content[1024];

    if (strcmp(tool_name, "write_file") == 0) {
        type = MEM_PATTERN;
        const char *path = strstr(tool_args, "\"path\"");
        snprintf(content, sizeof(content), "wrote file %s",
                 path ? path : "(unknown)");
    } else if (strcmp(tool_name, "edit_file") == 0) {
        type = MEM_PATTERN;
        const char *path = strstr(tool_args, "\"path\"");
        snprintf(content, sizeof(content), "edited file %s",
                 path ? path : "(unknown)");
    } else if (strcmp(tool_name, "bash") == 0) {
        type = MEM_WORKFLOW;
        const char *cmd = strstr(tool_args, "\"command\"");
        snprintf(content, sizeof(content), "ran: %.200s",
                 cmd ? cmd : "(unknown)");
    } else if (strcmp(tool_name, "read_file") == 0) {
        type = MEM_FACT;
        const char *path = strstr(tool_args, "\"path\"");
        snprintf(content, sizeof(content), "read file %s",
                 path ? path : "(unknown)");
    } else {
        return; /* ignore other tools */
    }

    obs_add(content, type);
}

/* ── LLM consolidation helpers ───────────────────────────────────── */

static const char *EXTRACT_SYSTEM =
    "You are a memory extraction engine. Given a list of tool call "
    "observations from a coding session, extract concise knowledge entries.\n\n"
    "For each observation, output ONE line in this exact format:\n"
    "- [type] knowledge\n\n"
    "Where type is one of: pattern, preference, architecture, bug, workflow, fact.\n"
    "Keep each entry under 100 chars. Skip trivial observations.\n"
    "Output ONLY the lines, no headers or explanation.";

static const char *MERGE_SYSTEM =
    "You are a memory consolidation engine. Merge the NEW memories into the "
    "EXISTING memory file.\n\n"
    "Rules:\n"
    "1. Remove duplicates (same knowledge, different wording)\n"
    "2. Update outdated entries with newer info\n"
    "3. Group by category using ## headers:\n"
    "   ## Patterns\n"
    "   ## Preferences\n"
    "   ## Architecture\n"
    "   ## Bugs\n"
    "   ## Workflows\n"
    "   ## Facts\n"
    "4. Each entry is one line: - knowledge\n"
    "5. Keep entries concise (under 100 chars each)\n"
    "6. Empty categories can be omitted\n"
    "7. Output ONLY the markdown, no explanation";

/* Build observations text for LLM */
static char *build_observations_text(void) {
    size_t cap = 4096;
    size_t len = 0;
    char *buf = xmalloc(cap);
    buf[0] = '\0';

    for (int i = 0; i < obs_count; i++) {
        const char *type_str = memory_type_str(obs_buf[i].type);
        size_t need = strlen(type_str) + strlen(obs_buf[i].content) + 16;
        while (len + need > cap) { cap *= 2; buf = xrealloc(buf, cap); }
        int n = snprintf(buf + len, cap - len, "- [%s] %s\n",
                         type_str, obs_buf[i].content);
        if (n > 0) len += (size_t)n;
    }
    return buf;
}

/* Call LLM with a single user message, return resp.content */
static int llm_extract(const char *system_prompt, const char *user_text,
                       char **out_content, char *err, size_t err_cap) {
    MessageList ml;
    msg_list_init(&ml);
    msg_list_push(&ml, msg_user_json(user_text));

    LLMResponse resp;
    memset(&resp, 0, sizeof(resp));
    int rc = llm_chat(&ml, system_prompt, g_config.model, &resp, err, err_cap);
    msg_list_free(&ml);

    if (rc != 0)
        return -1;

    if (!resp.content || !resp.content[0]) {
        llm_response_free(&resp);
        return -1;
    }

    *out_content = resp.content;
    resp.content = NULL; /* steal ownership */
    llm_response_free(&resp);
    return 0;
}

/* ── Consolidation: short-term → long-term ───────────────────────── */

int memory_consolidate(char *err, size_t err_cap) {
    if (obs_count == 0)
        return 0;

    /* Step 1: observations → new memories */
    char *obs_text = build_observations_text();
    char *new_memories = NULL;

    fprintf(stderr, "[memory] consolidating %d observations...\n", obs_count);

    if (llm_extract(EXTRACT_SYSTEM, obs_text, &new_memories, err, err_cap) < 0) {
        fprintf(stderr, "[memory] extraction failed: %s\n", err);
        free(obs_text);
        return -1;
    }
    free(obs_text);

    fprintf(stderr, "[memory] extracted:\n%s\n", new_memories);

    /* Step 2: new memories + existing memory.md → merged */
    char *existing = memory_recall();
    char *merge_input;
    if (existing && existing[0]) {
        merge_input = xasprintf("## EXISTING memories:\n%s\n\n## NEW memories:\n%s",
                                existing, new_memories);
    } else {
        merge_input = xasprintf("## NEW memories:\n%s", new_memories);
    }
    free(existing);
    free(new_memories);

    char *merged = NULL;
    if (llm_extract(MERGE_SYSTEM, merge_input, &merged, err, err_cap) < 0) {
        fprintf(stderr, "[memory] merge failed: %s\n", err);
        free(merge_input);
        return -1;
    }
    free(merge_input);

    /* Write merged result to memory.md */
    char path[PATH_MAX];
    memory_path(path, sizeof(path));
    FILE *f = fopen(path, "w");
    if (!f) {
        snprintf(err, err_cap, "cannot write memory.md");
        free(merged);
        return -1;
    }
    fprintf(f, "%s\n", merged);
    fflush(f);
    fclose(f);
    free(merged);

    /* Clear observations */
    obs_clear();

    fprintf(stderr, "[memory] consolidation complete\n");
    return 0;
}

/* ── System prompt injection ─────────────────────────────────────── */

char *memory_build_prompt(void) {
    char *text = memory_recall();
    if (!text || !text[0]) {
        free(text);
        return xstrdup("");
    }
    return text;
}
