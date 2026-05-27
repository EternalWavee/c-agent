/*
 * current_time tool — 返回当前系统时间，让 agent 获得时间感知能力。
 */
#include "tools/tools.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static ToolResult tool_current_time(cJSON *args) {
  (void)args;

  time_t now = time(NULL);
  struct tm *t = localtime(&now);

  char buf[128];
  strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S %Z", t);

  return (ToolResult){.ok = true, .output = xasprintf("%s", buf)};
}

ToolDef current_time_def = {
    .name = "current_time",
    .desc = "Get the current system time. "
            "Returns a formatted string like '2025-03-20 14:30:00 CST'. "
            "Use this tool whenever you need to know the current date or time.",
    .param_schema =
        "{\"type\":\"object\","
        "\"properties\":{},"
        "\"required\":[]}",
    .exec = tool_current_time,
    .read_only = true,
};