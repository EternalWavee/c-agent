#include "context/context.h"
#include "context/internal.h"
#include "core/config.h"
#include "agent/llm_client.h"
#include "core/message.h"
#include "core/util.h"
#include "cJSON.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool summary_should_apply(Context *ctx) {
    return ctx_budget_usage(ctx) > g_config.summary_threshold;
}

static int summary_apply(Context *ctx, char *err, size_t err_cap) {
    MessageList *hist = (MessageList *)ctx_history(ctx);
    int prefix_count = hist->len - KEEP_RECENT_MSGS;
    if (prefix_count <= 0)
        return 0;

    /* 把前 prefix_count 条消息压缩成一条摘要文本 */
    size_t buf_cap = 4096;
    size_t buf_len = 0;
    char *buf = xmalloc(buf_cap);
    buf[0] = '\0';

    for (int i = 0; i < prefix_count; i++) {
        cJSON *msg = cJSON_Parse(hist->items[i]);
        if (!msg) continue;
        const char *role = json_str(msg, "role");
        const char *content = json_str(msg, "content");
        if (role && content) {
            size_t need = strlen(role) + strlen(content) + 4;
            while (buf_len + need > buf_cap) {
                buf_cap *= 2;
                buf = xrealloc(buf, buf_cap);
            }
            int n = snprintf(buf + buf_len, buf_cap - buf_len,
                             "%s: %s\n", role, content);
            if (n > 0) buf_len += (size_t)n;
        }
        cJSON_Delete(msg);
    }

    /* 构建单条 user 消息发送给 LLM */
    MessageList prefix;
    msg_list_init(&prefix);
    msg_list_push(&prefix, msg_user_json(buf));
    free(buf);

    LLMResponse resp;
    memset(&resp, 0, sizeof(resp));
    const char *system = "You are a conversation summarizer. "
        "Summarize the following conversation into a concise handoff message. "
        "Preserve: key facts, decisions, outcomes, user identity and preferences, "
        "project context, and any unresolved tasks. "
        "Write as if the next assistant will read this and continue the conversation seamlessly.";

    int rc = llm_chat(&prefix, system, g_config.model, &resp, err, err_cap);
    msg_list_free(&prefix);

    if (rc != 0)
        return -1;

    if (!resp.content || !resp.content[0]) {
        llm_response_free(&resp);
        return -1;
    }

    /* 构造 summary 消息，role 为 user */
    cJSON *summary_msg = cJSON_CreateObject();
    cJSON_AddStringToObject(summary_msg, "role", "user");
    cJSON_AddStringToObject(summary_msg, "content", resp.content);
    char *summary_json = cJSON_PrintUnformatted(summary_msg);
    cJSON_Delete(summary_msg);

    ctx_replace_range(ctx, 0, prefix_count, summary_json);

    llm_response_free(&resp);
    return 0;
}

ContextPolicy summary_policy = {
    .name = "summary",
    .should_apply = summary_should_apply,
    .apply = summary_apply,
};
