#ifndef MEMORY_H
#define MEMORY_H

#include <stddef.h>

/*
 * Project-level persistent memory with short-term → long-term consolidation.
 *
 * Short-term: in-memory observation buffer, auto-captured from tool calls.
 * Long-term: .agent/memory.md, categorized markdown, injected into system prompt.
 *
 * On session shutdown, observations are consolidated into long-term memory
 * via two LLM calls: extract → merge.
 */

#define MEMORY_OBS_MAX 64

typedef enum {
    MEM_PATTERN,       /* coding patterns / conventions */
    MEM_PREFERENCE,    /* user preferences */
    MEM_ARCHITECTURE,  /* system architecture knowledge */
    MEM_BUG,           /* known bugs / issues */
    MEM_WORKFLOW,      /* build / test / deploy workflows */
    MEM_FACT,          /* general facts */
} MemoryType;

typedef struct {
    char *content;     /* malloc'd */
    MemoryType type;
} MemoryObservation;

/* Lifecycle */
int  memory_init(void);
void memory_shutdown(void);

/* Append a typed memory entry to .agent/memory.md */
int  memory_remember(const char *content, MemoryType type);

/* Read full memory.md (caller frees) */
char *memory_recall(void);

/* Auto-capture: store observation in short-term buffer (no disk write) */
void memory_observe(const char *tool_name, const char *tool_args,
                    const char *tool_output);

/* Consolidate: short-term observations → long-term memory.md (two LLM calls) */
int  memory_consolidate(char *err, size_t err_cap);

/* Build system prompt snippet from memory.md (caller frees) */
char *memory_build_prompt(void);

/* Convert MemoryType to string */
const char *memory_type_str(MemoryType type);

#endif
