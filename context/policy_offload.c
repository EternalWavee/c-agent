#include "context/context.h"
#include "context/internal.h"
#include "config.h"
#include "cJSON.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>

extern const char *session_current_id(void) __attribute__((weak));

static void json_escape_preview(const char *src, char *dst, size_t cap) {
    size_t wi = 0;
    for (size_t ri = 0; src && src[ri] && wi + 1 < cap; ri++) {
        unsigned char c = (unsigned char)src[ri];
        if (c == '\n' || c == '\r' || c == '\t')
            c = ' ';
        if (c == '"' || c == '\\') {
            if (wi + 2 >= cap)
                break;
            dst[wi++] = '\\';
            dst[wi++] = (char)c;
        } else if (c >= 0x20) {
            dst[wi++] = (char)c;
        }
    }
    dst[wi] = '\0';
}

static char *find_tool_source(MessageList *hist, int before_index,
                              const char *tool_call_id) {
    if (!tool_call_id || !tool_call_id[0])
        return xstrdup("unknown tool call");

    for (int i = before_index - 1; i >= 0; i--) {
        cJSON *msg = cJSON_Parse(hist->items[i]);
        if (!msg)
            continue;
        cJSON *calls = cJSON_GetObjectItem(msg, "tool_calls");
        if (!calls || !cJSON_IsArray(calls)) {
            cJSON_Delete(msg);
            continue;
        }

        int n = cJSON_GetArraySize(calls);
        for (int j = 0; j < n; j++) {
            cJSON *call = cJSON_GetArrayItem(calls, j);
            const char *id = json_str(call, "id");
            if (!id || strcmp(id, tool_call_id) != 0)
                continue;

            cJSON *fn = cJSON_GetObjectItem(call, "function");
            const char *name = fn ? json_str(fn, "name") : NULL;
            const char *args = fn ? json_str(fn, "arguments") : NULL;
            char *source = xasprintf("%s %s", name ? name : "unknown",
                                     args ? args : "{}");
            cJSON_Delete(msg);
            return source;
        }
        cJSON_Delete(msg);
    }

    return xasprintf("tool_call_id %s", tool_call_id);
}

static int append_manifest_entry(const char *offload_dir, int id,
                                 const char *rel_path, size_t content_len,
                                 const char *tool_call_id, const char *source,
                                 const char *preview) {
    char manifest_path[PATH_MAX];
    snprintf(manifest_path, sizeof(manifest_path), "%s/MANIFEST.md", offload_dir);

    FILE *f = fopen(manifest_path, "a");
    if (!f)
        return -1;

    char clean_preview[241];
    json_escape_preview(preview, clean_preview, sizeof(clean_preview));
    fprintf(f,
            "- id: %d\n"
            "  path: %s\n"
            "  tool_call_id: %s\n"
            "  source: %s\n"
            "  size_bytes: %zu\n"
            "  preview: \"%s\"\n",
            id, rel_path, tool_call_id ? tool_call_id : "", source ? source : "unknown",
            content_len, clean_preview);
    fclose(f);
    return 0;
}

static bool offload_should_apply(Context *ctx){
    return ctx_budget_usage(ctx) > g_config.offload_threshold;
}

static int offload_apply(Context *ctx, char *err, size_t err_cap) {
    char agent_path[PATH_MAX];
    snprintf(agent_path, sizeof(agent_path), "%s/.agent", g_config.workdir);
    mkdir(agent_path, 0755);

    char offload_root[PATH_MAX];
    snprintf(offload_root, sizeof(offload_root), "%s/.agent/offload", g_config.workdir);
    mkdir(offload_root, 0755);

    const char *session_id = NULL;
    if (session_current_id)
        session_id = session_current_id();

    char offload_dir[PATH_MAX];
    char offload_rel_dir[PATH_MAX];
    if (session_id && session_id[0]) {
        snprintf(offload_dir, sizeof(offload_dir), "%s/%s", offload_root, session_id);
        snprintf(offload_rel_dir, sizeof(offload_rel_dir), ".agent/offload/%s", session_id);
        mkdir(offload_dir, 0755);
    } else {
        snprintf(offload_dir, sizeof(offload_dir), "%s", offload_root);
        snprintf(offload_rel_dir, sizeof(offload_rel_dir), ".agent/offload");
    }

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
        char rel_path[PATH_MAX];
        snprintf(file_path, sizeof(file_path), "%s/%d.txt", offload_dir, id);
        snprintf(rel_path, sizeof(rel_path), "%s/%d.txt", offload_rel_dir, id);

        FILE *f = fopen(file_path, "w");
        if (!f) {
            snprintf(err, err_cap, "cannot create offload file %s", file_path);
            cJSON_Delete(msg);
            return -1;
        }
        fwrite(content, 1, strlen(content), f);
        fclose(f);
        ctx->next_offload_id++;

        size_t content_len = strlen(content);
        size_t preview_len = content_len;
        if (preview_len > 80)
            preview_len = 80;
        char preview[81];
        memcpy(preview, content, preview_len);
        preview[preview_len] = '\0';

        char *new_content = xasprintf(
            "%s\n"
            "[OFFLOADED_TOOL_OUTPUT]\n"
            "id: %d\n"
            "path: %s\n"
            "size_bytes: %zu\n"
            "manifest: %s/MANIFEST.md\n"
            "recovery: call read_file with {\"path\":\"%s\"} before relying on omitted content.",
            preview, id, rel_path, content_len, offload_rel_dir, rel_path);

        cJSON *new_msg = cJSON_CreateObject();
        cJSON_AddStringToObject(new_msg, "role", "tool");
        cJSON_AddStringToObject(new_msg, "content", new_content);
        const char *tcid = json_str(msg, "tool_call_id");
        if (tcid)
            cJSON_AddStringToObject(new_msg, "tool_call_id", tcid);

        char *source = find_tool_source(hist, i, tcid);
        if (append_manifest_entry(offload_dir, id, rel_path, content_len, tcid,
                                  source, preview) < 0) {
            snprintf(err, err_cap, "cannot update offload manifest");
            free(source);
            cJSON_Delete(new_msg);
            free(new_content);
            cJSON_Delete(msg);
            return -1;
        }
        free(source);

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