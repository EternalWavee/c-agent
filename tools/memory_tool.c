/*
 * memory_tool.c — remember and recall tools for the LLM.
 *
 * remember: write knowledge to .agent/memory.md
 * recall:   read .agent/memory.md and return its content
 */
#include "tools/tools.h"
#include "memory.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static MemoryType parse_type(const char *str) {
    if (!str) return MEM_FACT;
    if (strcmp(str, "pattern") == 0) return MEM_PATTERN;
    if (strcmp(str, "preference") == 0) return MEM_PREFERENCE;
    if (strcmp(str, "architecture") == 0) return MEM_ARCHITECTURE;
    if (strcmp(str, "bug") == 0) return MEM_BUG;
    if (strcmp(str, "workflow") == 0) return MEM_WORKFLOW;
    if (strcmp(str, "fact") == 0) return MEM_FACT;
    return MEM_FACT;
}

static ToolResult tool_remember(cJSON *args) {
    const char *content = cJSON_GetStringValue(cJSON_GetObjectItem(args, "content"));
    if (!content || !content[0])
        return (ToolResult){.ok = false,
                            .output = xstrdup("missing 'content' argument")};

    const char *type_str = cJSON_GetStringValue(cJSON_GetObjectItem(args, "type"));
    MemoryType type = parse_type(type_str);

    if (memory_remember(content, type) < 0)
        return (ToolResult){.ok = false,
                            .output = xstrdup("failed to write to memory file")};

    return (ToolResult){
        .ok = true,
        .output = xasprintf("Remembered [%s]: %.200s", memory_type_str(type), content),
    };
}

static ToolResult tool_recall(cJSON *args) {
    const char *keyword = cJSON_GetStringValue(cJSON_GetObjectItem(args, "keyword"));
    const char *type_str = cJSON_GetStringValue(cJSON_GetObjectItem(args, "type"));

    /* If filters provided, use filtered recall */
    if ((keyword && keyword[0]) || (type_str && type_str[0])) {
        char *filtered = memory_recall_filtered(keyword, type_str);
        if (!filtered)
            return (ToolResult){.ok = true,
                                .output = xstrdup("No matching memories found.")};
        return (ToolResult){.ok = true, .output = filtered};
    }

    /* No filters — return full memory with indices */
    char *content = memory_recall();
    if (!content)
        return (ToolResult){.ok = true,
                            .output = xstrdup("No memories stored yet.")};

    /* Add line indices for entries so LLM can reference them for delete/update */
    int count = 0;
    int line_cap = 64;
    char **line_arr = xmalloc((size_t)line_cap * sizeof(char *));

    /* Split content into lines */
    const char *p = content;
    while (*p) {
        const char *nl = strchr(p, '\n');
        size_t len = nl ? (size_t)(nl - p) : strlen(p);
        if (count >= line_cap) { line_cap *= 2; line_arr = xrealloc(line_arr, (size_t)line_cap * sizeof(char *)); }
        line_arr[count] = xmalloc(len + 1);
        memcpy(line_arr[count], p, len);
        line_arr[count][len] = '\0';
        count++;
        p = nl ? nl + 1 : p + len;
    }
    free(content);

    /* Rebuild with indices for memory entries */
    size_t cap = 4096, len = 0;
    char *buf = xmalloc(cap);
    buf[0] = '\0';

    int entry_idx = 0;
    for (int i = 0; i < count; i++) {
        if (strncmp(line_arr[i], "- [", 3) == 0) {
            /* Memory entry: add index */
            size_t need = strlen(line_arr[i]) + 32;
            while (len + need > cap) { cap *= 2; buf = xrealloc(buf, cap); }
            int n = snprintf(buf + len, cap - len, "[%d] %s\n", entry_idx, line_arr[i] + 2);
            if (n > 0) len += (size_t)n;
            entry_idx++;
        } else {
            /* Header or empty line: keep as-is */
            size_t need = strlen(line_arr[i]) + 2;
            while (len + need > cap) { cap *= 2; buf = xrealloc(buf, cap); }
            int n = snprintf(buf + len, cap - len, "%s\n", line_arr[i]);
            if (n > 0) len += (size_t)n;
        }
    }

    for (int i = 0; i < count; i++) free(line_arr[i]);
    free(line_arr);
    return (ToolResult){.ok = true, .output = buf};
}

/* ── memory_delete ──────────────────────────────────────────────── */

static ToolResult tool_memory_delete(cJSON *args) {
    cJSON *idx_obj = cJSON_GetObjectItem(args, "index");
    if (!idx_obj || !cJSON_IsNumber(idx_obj))
        return (ToolResult){.ok = false,
                            .output = xstrdup("missing 'index' argument")};

    int index = (int)cJSON_GetNumberValue(idx_obj);
    if (memory_delete(index) < 0)
        return (ToolResult){.ok = false,
                            .output = xasprintf("failed to delete memory at index %d", index)};

    return (ToolResult){.ok = true,
                        .output = xasprintf("Deleted memory entry %d", index)};
}

/* ── memory_update ──────────────────────────────────────────────── */

static ToolResult tool_memory_update(cJSON *args) {
    cJSON *idx_obj = cJSON_GetObjectItem(args, "index");
    if (!idx_obj || !cJSON_IsNumber(idx_obj))
        return (ToolResult){.ok = false,
                            .output = xstrdup("missing 'index' argument")};

    const char *content = cJSON_GetStringValue(cJSON_GetObjectItem(args, "content"));
    if (!content || !content[0])
        return (ToolResult){.ok = false,
                            .output = xstrdup("missing 'content' argument")};

    const char *type_str = cJSON_GetStringValue(cJSON_GetObjectItem(args, "type"));
    MemoryType type = parse_type(type_str);

    int index = (int)cJSON_GetNumberValue(idx_obj);
    if (memory_update(index, content, type) < 0)
        return (ToolResult){.ok = false,
                            .output = xasprintf("failed to update memory at index %d", index)};

    return (ToolResult){.ok = true,
                        .output = xasprintf("Updated memory entry %d", index)};
}

/* ── Tool definitions ────────────────────────────────────────────── */

ToolDef remember_tool_def = {
    .name = "remember",
    .desc = "Store important information into persistent project memory. "
            "The memory is saved to .agent/memory.md and persists across sessions. "
            "Use this to remember: project architecture, coding conventions, "
            "user preferences, past decisions, build commands, key file locations, "
            "or anything worth knowing in future sessions.",
    .param_schema =
        "{\"type\":\"object\","
        "\"properties\":{"
        "\"content\":{\"type\":\"string\","
        "\"description\":\"The knowledge to remember — be specific and concise\"},"
        "\"type\":{\"type\":\"string\","
        "\"description\":\"Memory type\","
        "\"enum\":[\"pattern\",\"preference\",\"architecture\",\"bug\",\"workflow\",\"fact\"]}"
        "},\"required\":[\"content\"]}",
    .exec = tool_remember,
    .read_only = false,
};

ToolDef recall_tool_def = {
    .name = "recall",
    .desc = "Read the project memory file with optional filtering. "
            "Returns entries with [index] prefixes for use with memory_delete/memory_update. "
            "Without filters, returns all entries. With filters, returns matching entries only.",
    .param_schema =
        "{\"type\":\"object\","
        "\"properties\":{"
        "\"keyword\":{\"type\":\"string\","
        "\"description\":\"Filter entries containing this keyword\"},"
        "\"type\":{\"type\":\"string\","
        "\"description\":\"Filter by memory type\","
        "\"enum\":[\"pattern\",\"preference\",\"architecture\",\"bug\",\"workflow\",\"fact\"]}"
        "},\"required\":[]}",
    .exec = tool_recall,
    .read_only = true,
};

ToolDef memory_delete_def = {
    .name = "memory_delete",
    .desc = "Delete a memory entry by its index. Use recall first to see indices.",
    .param_schema =
        "{\"type\":\"object\","
        "\"properties\":{"
        "\"index\":{\"type\":\"integer\","
        "\"description\":\"The index of the memory entry to delete (from recall output)\"}"
        "},\"required\":[\"index\"]}",
    .exec = tool_memory_delete,
    .read_only = false,
};

ToolDef memory_update_def = {
    .name = "memory_update",
    .desc = "Update a memory entry by its index. Use recall first to see indices.",
    .param_schema =
        "{\"type\":\"object\","
        "\"properties\":{"
        "\"index\":{\"type\":\"integer\","
        "\"description\":\"The index of the memory entry to update (from recall output)\"},"
        "\"content\":{\"type\":\"string\","
        "\"description\":\"The new content for this memory entry\"},"
        "\"type\":{\"type\":\"string\","
        "\"description\":\"Memory type\","
        "\"enum\":[\"pattern\",\"preference\",\"architecture\",\"bug\",\"workflow\",\"fact\"]}"
        "},\"required\":[\"index\",\"content\"]}",
    .exec = tool_memory_update,
    .read_only = false,
};
