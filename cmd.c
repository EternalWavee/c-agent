#include "cmd.h"

#include "agent/agent.h"
#include "agent/llm_client.h"
#include "config.h"
#include "session.h"

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

static int parse_model_list(const char *buf, char **models, int max) {
  int count = 0;
  const char *p = buf;
  while (*p && count < max) {
    const char *nl = strchr(p, '\n');
    size_t len = nl ? (size_t)(nl - p) : strlen(p);
    if (len > 0) {
      models[count] = malloc(len + 1);
      memcpy(models[count], p, len);
      models[count][len] = '\0';
      count++;
    }
    if (!nl)
      break;
    p = nl + 1;
  }
  return count;
}

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

static void render_session_menu(char **previews, int count, int selected) {
  for (int i = 0; i < count; i++) {
    if (i == selected)
      printf("\033[7m");
    printf("  %s  \033[0m\n", previews[i][0] ? previews[i] : "(empty)");
  }
  printf("\033[90m  \xE2\x86\x91\xE2\x86\x93 move  Enter restore  "
         "Esc cancel\033[0m\n");
  printf("\033[%dA", count + 1);
  fflush(stdout);
}

static int interactive_session_select(Agent *a) {
  char **paths, **previews;
  int count;
  if (session_list(&paths, &previews, &count) < 0 || count == 0) {
    fprintf(stderr, "No saved sessions\n");
    return -1;
  }

  int selected = 0;

  if (enable_raw_mode() < 0) {
    fprintf(stderr, "Failed to enable raw terminal mode\n");
    session_list_free(paths, previews, count);
    return -1;
  }

  render_session_menu(previews, count, selected);

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
      render_session_menu(previews, count, selected);
    } else if (c == '\r' || c == '\n') {
      /* save current history before loading */
      MessageList *cur = agent_get_history(a);
      int cur_len = cur->len;
      char **cur_items = cur->items;
      cur->items = NULL;
      cur->len = 0;
      cur->cap = 0;

      if (session_load(paths[selected], a) == 0) {
        /* append current messages after old session's messages */
        MessageList *hist = agent_get_history(a);
        for (int i = 0; i < cur_len; i++)
          msg_list_push(hist, cur_items[i]);
        free(cur_items);

        session_set_current(paths[selected]);
        /* delete incomplete file A if different from restored file */
        session_delete_other(paths[selected]);
        result = 0;
      } else {
        /* restore failed, put current history back */
        cur->items = cur_items;
        cur->len = cur_len;
        cur->cap = cur_len;
      }
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
    printf("Session restored.\n");
  else
    printf("Cancelled.\n");

  session_list_free(paths, previews, count);
  return result;
}

/* --- Command implementations --- */

static void cmd_model(void) {
  interactive_model_select();
}

static void cmd_session(Agent *a) {
  interactive_session_select(a);
}

static void cmd_help(void) {
  printf("Commands:\n");
  printf("  /model    Interactive model picker\n");
  printf("  /session  Browse and restore saved sessions\n");
  printf("  /help     Show this help\n");
  printf("  exit/quit/q  Exit\n");
}

/* --- Dispatch --- */

int cmd_dispatch(const char *input, Agent *a) {
  if (input[0] != '/')
    return 0;

  if (strcmp(input, "/model") == 0) {
    cmd_model();
  } else if (strcmp(input, "/session") == 0) {
    cmd_session(a);
  } else if (strcmp(input, "/help") == 0) {
    cmd_help();
  } else {
    printf("Unknown command: %s (try /help)\n", input);
  }
  return 1;
}
