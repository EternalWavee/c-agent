#ifndef HOOKS_H
#define HOOKS_H

#include "agent/llm_client.h"
#include "tools/tools.h"

typedef enum {
  HOOK_BEFORE_TOOL,
  HOOK_AFTER_TOOL,
  HOOK_EVENT_COUNT
} HookEvent;

typedef struct {
  const LLMToolCall *call;
  const ToolDef *def;
  const char *args_json;
  const ToolResult *result; /* NULL for before-tool hooks */
  int index;
} HookToolEvent;

typedef void (*HookFn)(HookEvent event, const void *data);

void hooks_init(void);
int hooks_register(HookEvent event, HookFn fn);
void hooks_emit(HookEvent event, const void *data);

#endif /* HOOKS_H */
