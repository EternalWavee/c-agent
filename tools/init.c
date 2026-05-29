/*
 * tools_init — register all built-in tools at startup.
 *
 * Separated from registry.c so test binaries can link the registry
 * functions (tool_register, tool_find, tool_list) without pulling in
 * every tool definition and its transitive dependencies.
 */
#include "tools/tools.h"

extern ToolDef bash_def;
extern ToolDef read_file_def;
extern ToolDef write_file_def;
extern ToolDef edit_file_def;
extern ToolDef remember_tool_def;
extern ToolDef recall_tool_def;
extern ToolDef subagent_spawn_def;
extern ToolDef subagent_status_def;
extern ToolDef subagent_wait_def;
extern ToolDef memory_delete_def;
extern ToolDef memory_update_def;
extern ToolDef current_time_def;
extern ToolDef load_skill_def;

void tools_init(void) {
    tool_register(&bash_def);
    tool_register(&read_file_def);
    tool_register(&write_file_def);
    tool_register(&edit_file_def);
    tool_register(&remember_tool_def);
    tool_register(&recall_tool_def);
    tool_register(&subagent_spawn_def);
    tool_register(&subagent_status_def);
    tool_register(&subagent_wait_def);
    tool_register(&memory_delete_def);
    tool_register(&memory_update_def);
    tool_register(&current_time_def);
    tool_register(&load_skill_def);
}
