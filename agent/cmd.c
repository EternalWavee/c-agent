#include "agent/cmd.h"

#include "agent/agent.h"
#include "agent/llm_client.h"
#include "core/config.h"
#include "session/session.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#define MAX_MODELS 64

/* --- Terminal raw mode --- */

static struct termios orig_termios;
static int raw_mode_enabled = 0;

static void disable_raw_mode(void) {
  if (raw_mode_enabled) {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
    raw_mode_enabled = 0;
  }
}

static int enable_raw_mode(void) {
  if (!isatty(STDIN_FILENO))
    return -1;
  if (tcgetattr(STDIN_FILENO, &orig_termios) == -1)
    return -1;
  atexit(disable_raw_mode);

  struct termios raw = orig_termios;
  raw.c_lflag &= ~(ECHO | ICANON | ISIG);
  raw.c_iflag &= ~(IXON | ICRNL);
  raw.c_cc[VMIN] = 1;
  raw.c_cc[VTIME] = 0;
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1)
    return -1;
  raw_mode_enabled = 1;
  return 0;
}

/* --- Model list helpers --- */

static void free_model_list(char **models, int count) {
  for (int i = 0; i < count; i++)
    free(models[i]);
}

static int find_current_model(char **models, int count) {
  for (int i = 0; i < count; i++) {
    if (strcmp(models[i], g_config.model) == 0)
      return i;
  }
  return -1;
}

/* --- Interactive model selector --- */

static void render_model_menu(char **models, int count, int selected,
                              int show_hint) {
  for (int i = 0; i < count; i++) {
    if (i == selected)
      printf("\033[7m");
    printf("  %s  \033[0m\n", models[i]);
  }
  if (show_hint)
    printf("\033[90m  \xE2\x86\x91\xE2\x86\x93 move  Enter select  "
           "Esc cancel\033[0m\n");
  printf("\033[%dA", count + (show_hint ? 1 : 0));
  fflush(stdout);
}

typedef struct {
  const char *name;
  int context_window;
} ModelEntry;

static const ModelEntry BUILTIN_MODELS[] = {
    {"deepseek-chat",      32000},
    {"deepseek-v3.2",      32000},
    {"deepseek-reasoner",  32000},
    {"minimax",            192000},
    {"minimax-m2.7",       192000},
    {"glm",                128000},
    {"glm-5.1",            128000},
    {"qwen",               256000},
    {"qwen3.5-27b",        256000},
    {"qwen3coder",         32000},
};

static int interactive_model_select(void) {
  char *models[MAX_MODELS];
  int count = 0;
  for (int i = 0; i < (int)(sizeof(BUILTIN_MODELS) / sizeof(BUILTIN_MODELS[0])); i++)
    models[count++] = (char *)BUILTIN_MODELS[i].name;

  int selected = find_current_model(models, count);
  if (selected < 0)
    selected = 0;

  if (enable_raw_mode() < 0) {
    fprintf(stderr, "Failed to enable raw terminal mode\n");
    free_model_list(models, count);
    return -1;
  }

  render_model_menu(models, count, selected, 1);

  int result = -1;
  while (1) {
    char c;
    if (read(STDIN_FILENO, &c, 1) != 1)
      break;

    if (c == '\033') {
      char seq[2];
      if (read(STDIN_FILENO, &seq[0], 1) != 1)
        break;
      if (read(STDIN_FILENO, &seq[1], 1) != 1)
        break;
      if (seq[0] == '[') {
        if (seq[1] == 'A' && selected > 0)
          selected--;
        else if (seq[1] == 'B' && selected < count - 1)
          selected++;
      }
      render_model_menu(models, count, selected, 1);
    } else if (c == '\r' || c == '\n') {
      snprintf(g_config.model, sizeof(g_config.model), "%s", models[selected]);
      g_config.context_window = BUILTIN_MODELS[selected].context_window;
      result = 0;
      break;
    } else if (c == 'q' || c == 'Q') {
      break;
    }
  }

  disable_raw_mode();

  printf("\033[%dB", count + 1);
  for (int i = 0; i <= count; i++)
    printf("\033[2K\n");
  printf("\033[%dA", count + 1);

  if (result == 0)
    printf("Switched model to: %s (context: %d tokens)\n",
           g_config.model, g_config.context_window);
  else
    printf("Cancelled.\n");

  return result;
}

/* --- Interactive session selector --- */

static void render_session_menu(SessionEntry *entries, int count, int selected) {
  for (int i = 0; i < count; i++) {
    if (i == selected)
      printf("\033[7m");
    const char *label = entries[i].name[0] ? entries[i].name : entries[i].session_id;
    printf("  %s  \033[0m\n", label);
  }
  printf("\033[90m  \xE2\x86\x91\xE2\x86\x93 move  Enter restore  "
         "Esc cancel\033[0m\n");
  printf("\033[%dA", count + 1);
  fflush(stdout);
}

static void interactive_session_select(Agent *a) {
  SessionEntry *entries;
  int count = session_list(&entries);
  if (count <= 0) {
    fprintf(stderr, "No saved sessions\n");
    return;
  }

  int selected = 0;

  if (enable_raw_mode() < 0) {
    fprintf(stderr, "Failed to enable raw terminal mode\n");
    session_list_free(entries, count);
    return;
  }

  render_session_menu(entries, count, selected);

  int result = -1;
  while (1) {
    char c;
    if (read(STDIN_FILENO, &c, 1) != 1)
      break;

    if (c == '\033') {
      char seq[2];
      if (read(STDIN_FILENO, &seq[0], 1) != 1)
        break;
      if (read(STDIN_FILENO, &seq[1], 1) != 1)
        break;
      if (seq[0] == '[') {
        if (seq[1] == 'A' && selected > 0)
          selected--;
        else if (seq[1] == 'B' && selected < count - 1)
          selected++;
      }
      render_session_menu(entries, count, selected);
    } else if (c == '\r' || c == '\n') {
      if (session_restore(entries[selected].session_id, a) == 0)
        result = 0;
      break;
    } else if (c == 'q' || c == 'Q') {
      break;
    }
  }

  disable_raw_mode();

  printf("\033[%dB", count + 1);
  for (int i = 0; i <= count; i++)
    printf("\033[2K\n");
  printf("\033[%dA", count + 1);

  if (result == 0)
    printf("Session restored: %s\n", entries[selected].session_id);
  else
    printf("Cancelled.\n");

  session_list_free(entries, count);
}

/* --- Command implementations --- */

static void cmd_model(void) {
  interactive_model_select();
}

static void cmd_session(const char *args, Agent *a) {
  if (args[0] == '\0' || strcmp(args, "restore") == 0) {
    interactive_session_select(a);
  } else if (strcmp(args, "new") == 0) {
    if (session_new(a) == 0)
      printf("Created new session: %s\n", session_current_id());
    else
      fprintf(stderr, "Failed to create new session\n");
  } else if (strncmp(args, "name ", 5) == 0) {
    const char *name = args + 5;
    while (*name == ' ') name++;
    if (session_rename(name) == 0)
      printf("Session renamed to: %s\n", name);
    else
      fprintf(stderr, "Failed to rename session\n");
  } else if (strncmp(args, "delete ", 7) == 0) {
    const char *id = args + 7;
    while (*id == ' ') id++;
    if (session_delete(id) == 0)
      printf("Deleted session: %s\n", id);
    else
      fprintf(stderr, "Failed to delete session (cannot delete active session)\n");
  } else if (strncmp(args, "restore ", 8) == 0) {
    const char *id = args + 8;
    while (*id == ' ') id++;
    if (session_restore(id, a) == 0)
      printf("Restored session: %s\n", id);
    else
      fprintf(stderr, "Failed to restore session: %s\n", id);
  } else {
    fprintf(stderr, "Unknown /session subcommand: %s\n", args);
    printf("Usage: /session [new|name <name>|delete <id>|restore [id]]\n");
  }
}

static void cmd_memory(const char *args, Agent *a) {
  if (args[0] != '\0') {
    fprintf(stderr, "Usage: /memory — summarize and save what you learned\n");
    return;
  }
  /* Ask the LLM to summarize what it learned and save to memory */
  const char *prompt =
      "Look at our conversation so far. Use the remember tool to save "
      "anything important you learned about this project — architecture, "
      "conventions, key files, build commands, user preferences, decisions. "
      "Be specific and concise. Call remember once for each distinct piece of knowledge. "
      "If nothing noteworthy, say 'Nothing to remember yet.'";
  const char *reply = agent_chat(a, prompt);
  if (reply)
    printf("%s\n", reply);
}

static void cmd_help(void) {
  printf("Commands:\n");
  printf("  /model            Interactive model picker\n");
  printf("  /session          Browse and restore saved sessions\n");
  printf("  /session new      Create a new session\n");
  printf("  /session name X   Rename current session\n");
  printf("  /session delete X Delete a session\n");
  printf("  /session restore X Restore a session by id\n");
  printf("  /memory           Show project memory\n");
  printf("  /help             Show this help\n");
  printf("  exit/quit/q       Exit\n");
}

/* --- Dispatch --- */

int cmd_dispatch(const char *input, Agent *a) {
  if (input[0] != '/')
    return 0;

  if (strcmp(input, "/model") == 0) {
    cmd_model();
  } else if (strncmp(input, "/session", 8) == 0) {
    const char *args = input + 8;
    while (*args == ' ') args++;
    cmd_session(args, a);
  } else if (strncmp(input, "/memory", 7) == 0) {
    const char *args = input + 7;
    while (*args == ' ') args++;
    cmd_memory(args, a);
  } else if (strcmp(input, "/help") == 0) {
    cmd_help();
  } else {
    printf("Unknown command: %s (try /help)\n", input);
  }
  return 1;
}
