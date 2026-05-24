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

static ToolResult tool_remember(cJSON *args) {
    const char *content = cJSON_GetStringValue(cJSON_GetObjectItem(args, "content"));
    if (!content || !content[0])
        return (ToolResult){.ok = false,
                            .output = xstrdup("missing 'content' argument")};

    const char *category = cJSON_GetStringValue(cJSON_GetObjectItem(args, "category"));

    if (memory_remember(content, category) < 0)
        return (ToolResult){.ok = false,
                            .output = xstrdup("failed to write to memory file")};

    return (ToolResult){
        .ok = true,
        .output = xasprintf("Remembered: %.200s", content),
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
        "\"category\":{\"type\":\"string\","
        "\"description\":\"Category tag: architecture, convention, preference, "
        "decision, build, workflow, bug, fact\"}"
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
