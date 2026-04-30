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

#endif
