#ifndef MEMORY_H
#define MEMORY_H

#include <stddef.h>

/*
 * Project-level persistent memory.
 *
 * Short-term: in-memory observation buffer, auto-captured from tool calls.
 * Long-term: .agent/memory/MEMORY.md index plus typed memory files.
 *
 * The six memory types are stable: pattern, preference, architecture,
 * bug, workflow, and fact.
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

/* Append a typed memory entry to .agent/memory/<type>.md */
int  memory_remember(const char *content, MemoryType type);

/* Read all typed memory entries with stable global indices (caller frees) */
char *memory_recall(void);

/* Read filtered memory. keyword and/or type_str can be NULL (no filter).
 * Returns formatted string with 0-based line indices. Caller frees. */
char *memory_recall_filtered(const char *keyword, const char *type_str);

/* Delete memory entry at index (0-based). Returns 0 on success, -1 on error. */
int memory_delete(int index);

/* Replace memory entry at index with new content+type. Returns 0 on success, -1 on error. */
int memory_update(int index, const char *content, MemoryType type);

/* Auto-capture: store observation in short-term buffer (no disk write) */
void memory_observe(const char *tool_name, const char *tool_args,
                    const char *tool_output);

/* Number of pending short-term observations waiting for manual consolidation */
int  memory_observation_count(void);

/* Consolidate observations with a restricted memory LLM. */
int  memory_consolidate(char *err, size_t err_cap);

/* Build system prompt snippet from typed memory files (caller frees) */
char *memory_build_prompt(void);

/* Convert MemoryType to string */
const char *memory_type_str(MemoryType type);

#endif
