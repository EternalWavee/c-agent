/*
 * memory_tool.c — remember and recall tools for the LLM.
 *
 * remember: write knowledge to .agent/memory.md
 * recall:   read .agent/memory.md and return its content
 */
#include "tools/tools.h"
#include "memory/memory.h"
#include "core/util.h"

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
    (void)args;

    char *content = memory_recall();
    if (!content)
        return (ToolResult){.ok = true,
                            .output = xstrdup("No memories stored yet.")};

    return (ToolResult){.ok = true, .output = content};
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
    .desc = "Read the project memory file. Returns everything the agent has "
            "learned about this project across all sessions. Use this when you "
            "need context about past work, project structure, or decisions.",
    .param_schema =
        "{\"type\":\"object\","
        "\"properties\":{},"
        "\"required\":[]}",
    .exec = tool_recall,
    .read_only = true,
};
