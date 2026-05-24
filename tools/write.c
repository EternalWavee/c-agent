/*
 *   write_file tool - 写工具
 */

#include"tools/tools.h"
#include"tools/sandbox.h"
#include"config.h"
#include"util.h"

#include<errno.h>
#include<stdio.h>
#include<stdlib.h>
#include<string.h>


static ToolResult tool_write_file(cJSON *args){
    const char *path = cJSON_GetStringValue(cJSON_GetObjectItem(args,"path"));
    const char *content = cJSON_GetStringValue(cJSON_GetObjectItem(args,"content"));
    if(!path)
    return(ToolResult){.ok=false,
                        .output=xstrdup("missing 'path' argument")};
    if(!content)
    return(ToolResult){.ok=false,
                        .output=xstrdup("missing 'content' argument")};
    
    
    char *full_path = resolve_workspace_path(path);
    if (!full_path)
        return (ToolResult){
            .ok = false,
            .output = xasprintf("path '%s' is outside workspace", path)};
    
    
    FILE *f = fopen(full_path, "w");
    if (!f) {
        char *msg = xasprintf("cannot open '%s': %s", path, strerror(errno));
        free(full_path);
        return (ToolResult){.ok = false, .output = msg};
    }

    size_t written = fwrite(content,1,strlen(content),f);
    fclose(f);
    free(full_path);

    return (ToolResult){.ok=true, .output = xasprintf("wrote %zu bytes to %s",written,path)};
}

ToolDef write_file_def = {
    .name = "write_file",
    .desc = "Write contents to a certain file.",
    .param_schema = 
        "{\"type\":\"object\","
        "\"properties\":{\"path\":{\"type\":\"string\","
        "\"description\":\"Relative path inside the workspace\"},"
        "\"content\":{\"type\":\"string\","
        "\"description\":\"Full file contents to write\"}},"
        "\"required\":[\"path\",\"content\"]}",
    .exec = tool_write_file,
    .read_only = false,
};

