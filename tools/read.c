/*
 * read_file tool — 读取工作区内的文件内容。
 *
 * 注册表通过 extern 找到 read_file_def，然后 tool_register 注册它。
 * agent 不直接调用这个文件里的函数，而是通过 registry -> executor -> .exec 调用。
 */
#include "tools/tools.h"
#include "tools/sandbox.h"
#include "core/config.h"
#include "core/util.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * 工具执行函数。签名固定为 ToolResult fn(cJSON *args)。
 * args 是 LLM 传来的 JSON 参数，例如 {"path": "main.c", "limit": 10}
 */
static ToolResult tool_read_file(cJSON *args) {
  const char *path = cJSON_GetStringValue(cJSON_GetObjectItem(args, "path"));
  if (!path)
    return (ToolResult){.ok = false,
                        .output = xstrdup("missing 'path' argument")};

  cJSON *limit_obj = cJSON_GetObjectItem(args, "limit");
  int limit = (limit_obj && cJSON_IsNumber(limit_obj))
                  ? (int)cJSON_GetNumberValue(limit_obj)
                  : -1; 

  char *full_path = resolve_workspace_path(path);
  if (!full_path)
    return (ToolResult){
        .ok = false,
        .output = xasprintf("path '%s' is outside workspace", path)};

  FILE *f = fopen(full_path, "r");
  if (!f) {
    char *msg = xasprintf("cannot open '%s': %s", path, strerror(errno));
    free(full_path);
    return (ToolResult){.ok = false, .output = msg};
  }

  size_t cap = 4096;
  size_t len = 0;
  char *buf = xmalloc(cap);
  int lines = 0;
  char line[4096];

  while (fgets(line, sizeof(line), f)) {
    if (limit >= 0 && lines >= limit)
      break;

    size_t line_len = strlen(line);

    while (len + line_len + 1 > cap) {
      cap *= 2;
      buf = xrealloc(buf, cap);
    }

    memcpy(buf + len, line, line_len);
    len += line_len;
    lines++;

    if (len >= MAX_TOOL_OUTPUT) {
      len = MAX_TOOL_OUTPUT;
      break;
    }
  }

  fclose(f);
  free(full_path);

  buf[len] = '\0';
  if (len == 0)
    return (ToolResult){.ok = true, .output = xstrdup("(empty file)")};

  return (ToolResult){.ok = true, .output = buf};
}

ToolDef read_file_def = {
    .name = "read_file",
    .desc = "Read a file's contents from the workspace.",
    .param_schema =
        "{\"type\":\"object\","
        "\"properties\":{\"path\":{\"type\":\"string\","
        "\"description\":\"Relative path inside the workspace\"},"
        "\"limit\":{\"type\":\"integer\","
        "\"description\":\"Optional maximum number of lines to return\"}},"
        "\"required\":[\"path\"]}",
    .exec = tool_read_file,
    .read_only = true,
};
