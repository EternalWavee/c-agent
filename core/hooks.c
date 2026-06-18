#include "hooks.h"

#include "memory.h"

#include <pthread.h>
#include <stddef.h>

#define MAX_HOOKS_PER_EVENT 16

static HookFn g_hooks[HOOK_EVENT_COUNT][MAX_HOOKS_PER_EVENT];
static int g_hook_counts[HOOK_EVENT_COUNT];
static pthread_mutex_t g_hooks_lock = PTHREAD_MUTEX_INITIALIZER;

int hooks_register(HookEvent event, HookFn fn) {
  if (event < 0 || event >= HOOK_EVENT_COUNT || !fn)
    return -1;
  if (g_hook_counts[event] >= MAX_HOOKS_PER_EVENT)
    return -1;
  g_hooks[event][g_hook_counts[event]++] = fn;
  return 0;
}

void hooks_emit(HookEvent event, const void *data) {
  if (event < 0 || event >= HOOK_EVENT_COUNT)
    return;
  pthread_mutex_lock(&g_hooks_lock);
  for (int i = 0; i < g_hook_counts[event]; i++)
    g_hooks[event][i](event, data);
  pthread_mutex_unlock(&g_hooks_lock);
}

static void memory_tool_hook(HookEvent event, const void *data) {
  if (event != HOOK_AFTER_TOOL || !data)
    return;

  const HookToolEvent *ev = data;
  if (!ev->call || !ev->call->name)
    return;

  const char *output = "";
  if (ev->result && ev->result->output)
    output = ev->result->output;
  memory_observe(ev->call->name, ev->args_json ? ev->args_json : "{}", output);
}

void hooks_init(void) {
  for (int i = 0; i < HOOK_EVENT_COUNT; i++)
    g_hook_counts[i] = 0;

  hooks_register(HOOK_AFTER_TOOL, memory_tool_hook);
}
