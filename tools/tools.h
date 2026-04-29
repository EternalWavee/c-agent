#ifndef TOOLS_H
#define TOOLS_H

#include "cJSON.h"

#include <stdbool.h>

/*
 * Part 1: one concrete tool (bash). We expose its execution as a plain
 * function rather than dressing it up behind a registry — the crudeness is
 * intentional, and it is the seed that motivates the registry refactor in
 * Part 2.
 */

typedef struct {
  bool ok;      /* command exited cleanly */
  char *output; /* malloc'd, may be NULL ("no output" case) */
} ToolResult;

void tool_result_free(ToolResult *r);

/* ── bash ─────────────────────────────────────────── */

/* Tool schema fields — referenced by llm_client when building the request. */
extern const char *BASH_TOOL_NAME;
extern const char *BASH_TOOL_DESC;
extern const char *BASH_TOOL_SCHEMA; /* JSON Schema fragment as a raw string */

ToolResult bash_tool_exec(cJSON *args);

#endif
