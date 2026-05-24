#ifndef SESSION_H
#define SESSION_H

#include "agent/agent.h"

#define SESSION_ID_LEN    20
#define SESSION_NAME_MAX  128
#define SESSION_MODEL_MAX 128
#define SESSION_TAGS_MAX  16
#define SESSION_TAG_LEN   64

typedef struct {
    char id[SESSION_ID_LEN];
    char name[SESSION_NAME_MAX];
    char model[SESSION_MODEL_MAX];
    char created[32];
    char updated[32];
    int  message_count;
    char tags[SESSION_TAGS_MAX][SESSION_TAG_LEN];
    int  tag_count;
} SessionMeta;

typedef struct {
    char session_id[SESSION_ID_LEN];
    char name[SESSION_NAME_MAX];
    char model[SESSION_MODEL_MAX];
    char created[32];
    char updated[32];
    int  message_count;
    char preview[256];
} SessionEntry;

/* Lifecycle */
int  session_startup_recovery(Agent *a);
int  session_new(Agent *a);
int  session_append(Agent *a, int from);
int  session_checkpoint(Agent *a);
int  session_restore(const char *session_id, Agent *a);
void session_shutdown(Agent *a);

/* Metadata */
int  session_rename(const char *new_name);
int  session_add_tag(const char *tag);
int  session_delete(const char *session_id);

/* Listing */
int  session_list(SessionEntry **out);
void session_list_free(SessionEntry *list, int count);
const char *session_current_id(void);

#endif
