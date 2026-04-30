#ifndef SESSION_H
#define SESSION_H

#include "agent/agent.h"
#include "message.h"

/*
 * Save the current conversation to ~/.c-agent/sessions/<timestamp>.json.
 * Returns 0 on success, -1 on error.
 */
int session_save(const char *model, const MessageList *history);

/*
 * Load a session file into the agent, replacing current history.
 * Returns 0 on success, -1 on error.
 */
int session_load(const char *path, Agent *a);

/*
 * List all saved sessions. Each entry has a path and a preview
 * (last user message). Sorted by filename descending (newest first).
 * Caller must free with session_list_free().
 */
int session_list(char ***out_paths, char ***out_previews, int *out_count);
void session_list_free(char **paths, char **previews, int count);

/*
 * Set the current session path, so subsequent saves overwrite this file
 * instead of creating a new one. Used when restoring a session.
 */
void session_set_current(const char *path);

/*
 * Delete the current session file if it differs from `keep`.
 * Used to remove the incomplete session after merging into a restored one.
 */
void session_delete_other(const char *keep);

#endif
