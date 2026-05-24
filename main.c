#include "agent/agent.h"
#include "tools/tools.h"
#include "cmd.h"
#include "config.h"
#include "session.h"
#include "ui/ui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define INPUT_BUF 4096

int main(void) {
  config_init();

  tools_init();
  Agent *a = agent_create();
  if (!a) {
    fprintf(stderr, "agent_create failed\n");
    return 1;
  }

  /* Start with a fresh session */
  int prev_count = 0;
  if (isatty(STDIN_FILENO)) {
    session_new(a);
    prev_count = agent_history_count(a);
  }

  ui_init();
  ui_start();
  ui_banner();

  char input[INPUT_BUF];

  while (1) {
    ui_prompt();
    if (!fgets(input, sizeof(input), stdin)) {
      break;
    }
    size_t len = strlen(input);
    if (len > 0 && input[len - 1] == '\n')
      input[len - 1] = '\0';
    if (strcmp(input, "exit") == 0 || strcmp(input, "quit") == 0 ||
        strcmp(input, "q") == 0) {
      break;
    }

    if (cmd_dispatch(input, a))
      continue;

    const char *reply = agent_chat(a, input);
    if (reply) {
      ui_idle();
      printf("%s\n", reply);
      /* Append new messages to session log (interactive only) */
      if (isatty(STDIN_FILENO)) {
        if (session_append(a, prev_count) < 0)
          fprintf(stderr, "session append failed\n");
        prev_count = agent_history_count(a);
      }
    }
  }

  ui_stop();
  if (isatty(STDIN_FILENO))
    session_shutdown(a);
  agent_free(a);
  return 0;
}
