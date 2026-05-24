#include "core/config.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

AgentConfig g_config;

static void copy_env_string(char *dst, size_t cap, const char *name,
                            const char *fallback) {
  const char *value = getenv(name);
  snprintf(dst, cap, "%s", (value && value[0]) ? value : fallback);
}

static int parse_env_int(const char *name, int fallback, int min_value,
                         int max_value) {
  const char *value = getenv(name);
  if (!value || !value[0])
    return fallback;
  errno = 0;
  char *end = NULL;
  long parsed = strtol(value, &end, 10);
  if (errno != 0 || end == value || *end != '\0' || parsed < min_value ||
      parsed > max_value) {
    fprintf(stderr, "[config] warning: invalid %s=%s, using %d\n", name, value,
            fallback);
    return fallback;
  }
  return (int)parsed;
}

void config_init(void) {
  copy_env_string(g_config.model, sizeof(g_config.model), "MODEL_ID",
                  "deepseek-chat");
  copy_env_string(g_config.llm_host, sizeof(g_config.llm_host), "LLM_HOST",
                  "127.0.0.1");
  copy_env_string(g_config.api_key, sizeof(g_config.api_key), "API_KEY",
                  "none");

  g_config.llm_port = parse_env_int("LLM_PORT", 18080, 1, 65535);
  g_config.max_tokens = parse_env_int("MAX_TOKENS", 8000, 1, INT_MAX);
  g_config.context_window = parse_env_int("CONTEXT_WINDOW", 32000, 100, INT_MAX);
  const char *ot = getenv("OFFLOAD_THRESHOLD");
  g_config.offload_threshold = (ot && ot[0]) ? strtof(ot, NULL) : 0.8f;
  const char *st = getenv("SUMMARY_THRESHOLD");
  g_config.summary_threshold = (st && st[0]) ? strtof(st, NULL) : 0.8f;

  /* Canonicalize so tools and logs see the same path shape. */
  if (!realpath(".", g_config.workdir)) {
    if (!getcwd(g_config.workdir, sizeof(g_config.workdir))) {
      perror("getcwd");
      exit(1);
    }
  }
}
