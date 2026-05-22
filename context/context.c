/* Conversation context: message history plus budget-driven reclamation. */
#include "context/internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

Context *ctx_create(int context_window) {
  Context *ctx = calloc(1, sizeof(*ctx));
  if (!ctx)
    return NULL;
  msg_list_init(&ctx->history);
  ctx->context_window = context_window;
  return ctx;
}

void ctx_free(Context *ctx) {
  if (!ctx)
    return;
  msg_list_free(&ctx->history);
  free(ctx);
}

void ctx_add_policy(Context *ctx, ContextPolicy *policy) {
  if (ctx->policy_count >= MAX_POLICIES) {
    fprintf(stderr, "[context] warning: max policies reached, ignoring %s\n",
            policy->name);
    return;
  }
  ctx->policies[ctx->policy_count++] = policy;
}

void ctx_push(Context *ctx, char *msg_json) {
  msg_list_push(&ctx->history, msg_json);
}

int ctx_total_tokens(const Context *ctx) {
  int total = 0;
  for (int i = 0; i < ctx->history.len; i++)
    total += ctx_estimate_tokens(ctx->history.items[i]);
  return total;
}

float ctx_budget_usage(const Context *ctx) {
  if (ctx->context_window <= 0)
    return 0.0f;
  return (float)ctx_total_tokens(ctx) / (float)ctx->context_window;
}

const MessageList *ctx_history(const Context *ctx) { return &ctx->history; }

void ctx_replace_msg(Context *ctx, int index, char *new_json) {
  if (index < 0 || index >= ctx->history.len)
    return;
  free(ctx->history.items[index]);
  ctx->history.items[index] = new_json;
}

/*
 * Replace history[from:to) with a single message. Frees the removed ones and
 * shifts the tail left. Caller surrenders ownership of new_json.
 */
void ctx_replace_range(Context *ctx, int from, int to, char *new_json) {
  if (from < 0 || to > ctx->history.len || from >= to)
    return;

  for (int i = from; i < to; i++)
    free(ctx->history.items[i]);

  ctx->history.items[from] = new_json;

  int range = to - from;
  int tail = ctx->history.len - to;
  if (tail > 0) {
    memmove(&ctx->history.items[from + 1], &ctx->history.items[to],
            (size_t)tail * sizeof(char *));
  }
  ctx->history.len -= range - 1;
}

int ctx_reclaim(Context *ctx, char *err, size_t err_cap) {
  for (int i = 0; i < ctx->policy_count; i++) {
    ContextPolicy *p = ctx->policies[i];
    if (!p->should_apply(ctx))
      continue;

    int before = ctx_total_tokens(ctx);
    printf("[context] policy \"%s\" triggered at %.1f%% (%d tokens)\n", p->name,
           ctx_budget_usage(ctx) * 100.0f, before);

    if (p->apply(ctx, err, err_cap) != 0)
      return -1;

    printf("[context] policy \"%s\" complete: %d -> %d tokens\n", p->name,
           before, ctx_total_tokens(ctx));
  }

  int total = ctx_total_tokens(ctx);
  if (ctx->context_window > 0 && total > ctx->context_window) {
    snprintf(err, err_cap, "context window exceeded (%d > %d tokens)", total,
             ctx->context_window);
    return -1;
  }
  return 0;
}
