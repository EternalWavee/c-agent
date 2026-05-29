#include "tools/tools.h"
#include "skills.h"
#include "util.h"

#include <stdlib.h>

static ToolResult tool_load_skill(cJSON *args) {
    const char *name = cJSON_GetStringValue(cJSON_GetObjectItem(args, "name"));
    if (!name || !name[0])
        return (ToolResult){.ok = false, .output = xstrdup("missing 'name' argument")};

    char err[256];
    char *content = skills_load(name, err, sizeof(err));
    if (!content)
        return (ToolResult){.ok = false, .output = xstrdup(err)};

    return (ToolResult){.ok = true, .output = content};
}

ToolDef load_skill_def = {
    .name = "load_skill",
    .desc = "Load the full SKILL.md for an available skill. Use this when a listed skill is relevant before proceeding with the task. Skill packages live under .agent/skills/<name>/ and may include scripts, references, and assets.",
    .param_schema =
        "{\"type\":\"object\","
        "\"properties\":{"
        "\"name\":{\"type\":\"string\","
        "\"description\":\"Skill directory/name from the Available Skills manifest\"}"
        "},\"required\":[\"name\"]}",
    .exec = tool_load_skill,
    .read_only = true,
};
