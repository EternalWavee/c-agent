#ifndef MEMORY_H
#define MEMORY_H

/*
 * Project-level persistent memory.
 *
 * All memory lives in a single file: .agent/memory.md
 * Three write paths:
 *   1. Auto-capture from tool execution (write_file/edit_file)
 *   2. LLM actively calls the remember tool
 *   3. User manually edits the file
 *
 * The LLM reads memory.md via read_file or the recall tool when needed.
 */

/* Lifecycle */
int  memory_init(void);
void memory_shutdown(void);

/* Append a memory entry to .agent/memory.md */
int  memory_remember(const char *content, const char *category);

/* Read and return the full content of .agent/memory.md (caller frees) */
char *memory_recall(void);


#endif
