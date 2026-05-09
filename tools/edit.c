/*
 *   edit_file tool - 编辑工具
 */

#include"tools/tools.h"
#include"tools/sandbox.h"
#include"config.h"
#include"util.h"

#include<errno.h>
#include<stdio.h>
#include<stdlib.h>
#include<string.h>

static ToolResult tool_edit_file(cJSON *args){
    const char *path = cJSON_GetStringValue(cJSON_GetObjectItem(args,"path"));
    const char *old_text = cJSON_GetStringValue(cJSON_GetObjectItem(args,"old_text"));
    const char *new_text = cJSON_GetStringValue(cJSON_GetObjectItem(args,"new_text"));
    if(!path)
    return(ToolResult){.ok=false,
                        .output=xstrdup("missing 'path' argument")};
    if(!old_text)
    return(ToolResult){.ok=false,
                        .output=xstrdup("missing 'old_text' argument")};
    if(!new_text)
    return(ToolResult){.ok=false,
                        .output=xstrdup("missing 'new_text' argument")};
    
    char *full_path = resolve_workspace_path(path);
    if (!full_path)
        return (ToolResult){
            .ok = false,
            .output = xasprintf("path '%s' is outside workspace", path)};

    /* 读文件内容 */
    FILE *f = fopen(full_path, "r");
    if (!f) {
        char *msg = xasprintf("cannot open '%s': %s", path, strerror(errno));
        free(full_path);
        return (ToolResult){.ok = false, .output = msg};
    }

    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *buf = xmalloc((size_t)file_size + 1);
    fread(buf, 1, (size_t)file_size, f);
    buf[file_size] = '\0';
    fclose(f);

    /* 查找 old_text */
    char *pos = strstr(buf, old_text);
    if (!pos) {
        free(buf);
        free(full_path);
        return (ToolResult){.ok = false,
                            .output = xasprintf("'%s' not found in %s", old_text, path)};
    }

    /* 构造新内容：前缀 + new_text + old_text 之后的部分 */
    size_t old_len = strlen(old_text);
    size_t new_len = strlen(new_text);
    size_t prefix_len = (size_t)(pos - buf);
    size_t suffix_len = strlen(pos + old_len);
    size_t result_len = prefix_len + new_len + suffix_len;

    char *result = xmalloc(result_len + 1);
    memcpy(result, buf, prefix_len);
    memcpy(result + prefix_len, new_text, new_len);
    memcpy(result + prefix_len + new_len, pos + old_len, suffix_len + 1);

    free(buf);

    /* 写回文件 */
    f = fopen(full_path, "w");
    if (!f) {
        char *msg = xasprintf("cannot write '%s': %s", path, strerror(errno));
        free(result);
        free(full_path);
        return (ToolResult){.ok = false, .output = msg};
    }

    fwrite(result, 1, result_len, f);
    fclose(f);
    free(result);
    free(full_path);

    return (ToolResult){.ok = true, .output = xasprintf("edited %s", path)};
}

ToolDef edit_file_def = {
    .name = "edit_file",
    .desc = "Replace the first occurrence of old_text with new_text in a file.",
    .param_schema =
        "{\"type\":\"object\","
        "\"properties\":{\"path\":{\"type\":\"string\","
        "\"description\":\"Relative path inside the workspace\"},"
        "\"old_text\":{\"type\":\"string\","
        "\"description\":\"Exact substring to find\"},"
        "\"new_text\":{\"type\":\"string\","
        "\"description\":\"Replacement text\"}},"
        "\"required\":[\"path\",\"old_text\",\"new_text\"]}",
    .exec = tool_edit_file,
};