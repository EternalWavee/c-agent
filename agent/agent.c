/*
 * agent.c — orchestration between user input, LLM turns, and tool execution.
 *
 * The skeleton below is sized for Phase A: one request in, one request out,
 * no persistent state to speak of. Phase B and Phase C will both require
 * you to revisit `struct Agent`, agent_create, and agent_free — treat what
 * is here as a starting point, not a contract.
 */
#include "agent.h"

#include "config.h"
#include "context/context.h"
#include "llm_client.h"
#include "memory.h"
#include "message.h"
#include "tools/tools.h"
#include "tools/executor.h"
#include "util.h"
#include "ui/ui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MAX_TURNS 20

static const char AGENT_SYSTEM_TEMPLATE[] =
    "You are a coding agent running in the CLI at %s.\n"
    "Use the provided tools when you need to run shell commands.\n"
    "Return a short, final text reply when the task is done.\n\n"
    "Memory:\n"
    "- Use the recall tool at the start of a task to check what you already know about this project.\n"
    "- Use the remember tool to store important findings: architecture decisions, build commands,\n"
    "  key file locations, coding conventions, user preferences, or anything worth knowing later.\n"
    "- Good memory entries are specific and concise. Example: 'Project uses cJSON for JSON parsing, source in libs/cJSON.c'\n"
    "- Memory persists across sessions. What you remember now will help you in future conversations.\n\n"
    "SubAgent:\n"
    "- Use subagent_spawn to start child agents in the background for independent subtasks.\n"
    "- Use subagent_status to check what subagents are running.\n"
    "- Use subagent_wait to get a subagent's result when it's done.\n"
    "- The child has its own context and runs autonomously — intermediate output stays out of your context.\n"
    "- Good use cases: exploring a codebase, running multiple checks, searching for patterns.\n"
    "- You can spawn multiple subagents for parallel work, then collect results.";

/* Thread-local flag: true when a subagent is running to suppress UI */
__thread bool g_agent_silent = false;

struct Agent {
  char *system_prompt;
  char *last_reply;
  Context *ctx;
  bool silent;  /* suppress UI output (for subagents) */
};

static Agent *agent_create_internal(bool silent) {
  Agent *a = calloc(1, sizeof(*a));
  if (!a)
    return NULL;
  a->silent = silent;

  char time_buf[64];
  time_t now = time(NULL);
  struct tm *tm_now = localtime(&now);
  strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S %Z", tm_now);

  char *base_prompt = xasprintf(AGENT_SYSTEM_TEMPLATE, g_config.workdir);
  char *ts_prompt = xasprintf("%s\n\n## Current Time\nCurrent system time: %s\n", base_prompt, time_buf);
  free(base_prompt);

  char *mem_text = memory_build_prompt();
  if (mem_text && mem_text[0])
    a->system_prompt = xasprintf("%s\n## Project Memory\n%s", ts_prompt, mem_text);
  else
    a->system_prompt = xstrdup(ts_prompt);
  free(ts_prompt);
  free(mem_text);

  a->ctx = ctx_create(g_config.context_window);
  ctx_add_policy(a->ctx, &offload_policy);
  ctx_add_policy(a->ctx, &summary_policy);
  return a;
}

Agent *agent_create(void) {
  return agent_create_internal(false);
}

Agent *agent_create_silent(void) {
  return agent_create_internal(true);
}

void agent_free(Agent *a) {
  if (!a)
    return;
  free(a->system_prompt);
  free(a->last_reply);
  ctx_free(a->ctx);
  free(a);
}

const char *agent_chat(Agent *a, const char *user_input) {
  g_agent_silent = a->silent;
  char *user_json = msg_user_json(user_input);
  ctx_push(a->ctx, user_json);

  char err[256];

  /* 回收上下文预算 */
  if (ctx_reclaim(a->ctx, err, sizeof(err)) < 0) {
    fprintf(stderr, "context reclaim error: %s\n", err);
    return NULL;
  }

  LLMResponse resp;
  memset(&resp, 0, sizeof(resp));
  if (!a->silent) ui_begin_thinking();
  if (llm_chat((MessageList *)ctx_history(a->ctx), a->system_prompt,
               g_config.model, &resp, err, sizeof(err)) < 0) {
    fprintf(stderr, "LLM error: %s\n", err);
    return NULL;
  }

  int turns = 0;
  while (resp.n_tool_calls > 0) {
    turns++;
    if (turns > MAX_TURNS) {
      a->last_reply = xstrdup("max turns exceeded");
      llm_response_free(&resp);
      return a->last_reply;
    }

    ctx_push(a->ctx, resp.raw_message);
    resp.raw_message = NULL;

    char *out_msgs[resp.n_tool_calls];
    if (executor_run_tools(resp.tool_calls, resp.n_tool_calls, out_msgs,
                           err, sizeof(err)) < 0) {
      fprintf(stderr, "executor error: %s\n", err);
      llm_response_free(&resp);
      return NULL;
    }

    /* Auto-capture observations for memory */
    for (int i = 0; i < resp.n_tool_calls; i++) {
      char *args_str = cJSON_PrintUnformatted(resp.tool_calls[i].args);
      memory_observe(resp.tool_calls[i].name, args_str,
                     out_msgs[i] ? out_msgs[i] : "");
      free(args_str);
    }

    for (int i = 0; i < resp.n_tool_calls; i++)
      ctx_push(a->ctx, out_msgs[i]);

    llm_response_free(&resp);
    memset(&resp, 0, sizeof(resp));

    /* 再次回收上下文预算 */
    if (ctx_reclaim(a->ctx, err, sizeof(err)) < 0) {
      fprintf(stderr, "context reclaim error: %s\n", err);
      return NULL;
    }

    if (!a->silent) ui_begin_thinking();
    if (llm_chat((MessageList *)ctx_history(a->ctx), a->system_prompt,
                 g_config.model, &resp, err, sizeof(err)) < 0) {
      fprintf(stderr, "llm_chat failed: %s\n", err);
      return NULL;
    }
  }

  free(a->last_reply);
  a->last_reply = resp.content ? resp.content : xstrdup("");
  if (resp.raw_message)
    ctx_push(a->ctx, resp.raw_message);
  free(resp.tool_calls);

  return a->last_reply;
}

MessageList *agent_get_history(Agent *a) {
  return (MessageList *)ctx_history(a->ctx);
}

void agent_clear_history(Agent *a) {
  ctx_free(a->ctx);
  a->ctx = ctx_create(g_config.context_window);
  ctx_add_policy(a->ctx, &offload_policy);
  ctx_add_policy(a->ctx, &summary_policy);
  free(a->last_reply);
  a->last_reply = NULL;
}

int agent_history_count(Agent *a) {
  const MessageList *hist = ctx_history(a->ctx);
  return hist->len;
}

const char *agent_get_message(Agent *a, int index) {
  const MessageList *hist = ctx_history(a->ctx);
  if (index < 0 || index >= hist->len)
    return NULL;
  return hist->items[index];
}

void agent_push_message(Agent *a, char *json) {
  ctx_push(a->ctx, json);
}