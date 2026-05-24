#include "context/context.h"
#include "context/internal.h"
#include "core/config.h"
#include "cJSON.h"
#include "core/util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>

static bool offload_should_apply(Context *ctx){
    return ctx_budget_usage(ctx) > g_config.offload_threshold;
}

static int offload_apply(Context *ctx, char *err, size_t err_cap) {
    char agent_path[PATH_MAX];
    snprintf(agent_path, sizeof(agent_path), "%s/.agent", g_config.workdir);
    mkdir(agent_path, 0755);

    char dir_path[PATH_MAX];
    snprintf(dir_path, sizeof(dir_path), "%s/.agent/offload", g_config.workdir);
    mkdir(dir_path, 0755);

    MessageList *hist = (MessageList *)ctx_history(ctx);
    int total = hist->len;
    int cutoff = total - KEEP_RECENT_MSGS;

    for (int i = 0; i < cutoff; i++) {
        cJSON *msg = cJSON_Parse(hist->items[i]);
        if (!msg) continue;

        const char *role = json_str(msg, "role");
        if (!role || strcmp(role, "tool") != 0) {
            cJSON_Delete(msg);
            continue;
        }

        const char *content = json_str(msg, "content");
        if (!content || strlen(content) < 100) {
            cJSON_Delete(msg);
            continue;
        }

        if (strstr(content, ".agent/offload/") != NULL) {
            cJSON_Delete(msg);
            continue;
        }

        int id = ctx->next_offload_id;
        char file_path[PATH_MAX];
        snprintf(file_path, sizeof(file_path), "%s/.agent/offload/%d.txt",
                 g_config.workdir, id);

        FILE *f = fopen(file_path, "w");
        if (!f) {
            snprintf(err, err_cap, "cannot create offload file %s", file_path);
            cJSON_Delete(msg);
            return -1;
        }
        fwrite(content, 1, strlen(content), f);
        fclose(f);
        ctx->next_offload_id++;

        size_t preview_len = strlen(content);
        if (preview_len > 80) preview_len = 80;
        char preview[81];
        memcpy(preview, content, preview_len);
        preview[preview_len] = '\0';

        char *new_content = xasprintf(
            "%s\n[...offloaded to .agent/offload/%d.txt — use read_file to recover]",
            preview, id);

        cJSON *new_msg = cJSON_CreateObject();
        cJSON_AddStringToObject(new_msg, "role", "tool");
        cJSON_AddStringToObject(new_msg, "content", new_content);
        const char *tcid = json_str(msg, "tool_call_id");
        if (tcid)
            cJSON_AddStringToObject(new_msg, "tool_call_id", tcid);

        char *new_json = cJSON_PrintUnformatted(new_msg);
        cJSON_Delete(new_msg);
        free(new_content);
        ctx_replace_msg(ctx, i, new_json);
        cJSON_Delete(msg);
    }

    return 0;
}

ContextPolicy offload_policy = {
    .name = "offload",
    .should_apply = offload_should_apply,
    .apply = offload_apply
};