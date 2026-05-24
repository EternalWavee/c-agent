#ifndef AGENT_H
#define AGENT_H

#include "message.h"

typedef struct Agent Agent;

Agent *agent_create(void);
void agent_free(Agent *a);

const char *agent_chat(Agent *a, const char *user_input);

/* Session persistence accessors */
MessageList *agent_get_history(Agent *a);
void agent_clear_history(Agent *a);

/* Opaque message accessors — session module uses these instead of
 * reaching into MessageList directly. */
int agent_history_count(Agent *a);
const char *agent_get_message(Agent *a, int index);
void agent_push_message(Agent *a, char *json);

#endif
