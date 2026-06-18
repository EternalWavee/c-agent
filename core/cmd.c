#include "cmd.h"

#include "agent/agent.h"
#include "agent/llm_client.h"
#include "config.h"
#include "memory.h"
#include "session.h"
#include "ui/markdown.h"
#include "ui/ui.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/select.h>
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

static int read_byte_timeout(char *out, int timeout_ms) {
  fd_set fds;
  FD_ZERO(&fds);
  FD_SET(STDIN_FILENO, &fds);

  struct timeval tv;
  tv.tv_sec = timeout_ms / 1000;
  tv.tv_usec = (timeout_ms % 1000) * 1000;

  int rc = select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv);
  if (rc <= 0)
    return rc;
  return read(STDIN_FILENO, out, 1) == 1 ? 1 : -1;
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
      if (read_byte_timeout(&seq[0], 80) != 1)
        break;
      if (read_byte_timeout(&seq[1], 80) != 1)
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

static void terminal_size(int *rows, int *cols) {
  struct winsize ws;
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_row > 0 &&
      ws.ws_col > 0) {
    *rows = ws.ws_row;
    *cols = ws.ws_col;
    return;
  }
  *rows = 24;
  *cols = 80;
}

static int session_visible_rows(int count) {
  int rows, cols;
  terminal_size(&rows, &cols);
  (void)cols;
  int visible = rows - 4; /* title + hint + prompt breathing room */
  if (visible < 3)
    visible = 3;
  if (visible > count)
    visible = count;
  return visible;
}

static void adjust_session_window(int selected, int count, int visible, int *top) {
  if (*top > selected)
    *top = selected;
  if (selected >= *top + visible)
    *top = selected - visible + 1;
  if (*top < 0)
    *top = 0;
  if (*top > count - visible)
    *top = count - visible;
  if (*top < 0)
    *top = 0;
}

static void format_size(long bytes, char *buf, size_t cap) {
  const char *units[] = {"B", "KB", "MB", "GB"};
  double value = bytes < 0 ? 0.0 : (double)bytes;
  int unit = 0;
  while (value >= 1024.0 && unit < 3) {
    value /= 1024.0;
    unit++;
  }
  if (unit == 0)
    snprintf(buf, cap, "%.0f%s", value, units[unit]);
  else if (value < 10.0)
    snprintf(buf, cap, "%.1f%s", value, units[unit]);
  else
    snprintf(buf, cap, "%.0f%s", value, units[unit]);
}

static void format_session_date(const char *iso, char *buf, size_t cap) {
  static const char *months[] = {
      "Jan", "Feb", "Mar", "Apr", "May", "Jun",
      "Jul", "Aug", "Sep", "Oct", "Nov", "Dec",
  };
  int year = 0, month = 0, day = 0, hour = 0, min = 0;
  if (iso &&
      sscanf(iso, "%d-%d-%dT%d:%d", &year, &month, &day, &hour, &min) == 5 &&
      month >= 1 && month <= 12 && day >= 1 && day <= 31 &&
      hour >= 0 && hour <= 23 && min >= 0 && min <= 59) {
    (void)year;
    snprintf(buf, cap, "%s %d %02d:%02d", months[month - 1], day, hour, min);
    return;
  }
  snprintf(buf, cap, "unknown");
}



static bool utf8_decode_one(const char *s, uint32_t *cp, int *bytes) {
  const unsigned char *p = (const unsigned char *)s;
  if (p[0] < 0x80) {
    *cp = p[0];
    *bytes = 1;
    return true;
  }
  if ((p[0] & 0xE0) == 0xC0 && p[1] && (p[1] & 0xC0) == 0x80) {
    *cp = ((uint32_t)(p[0] & 0x1F) << 6) | (uint32_t)(p[1] & 0x3F);
    *bytes = 2;
    return true;
  }
  if ((p[0] & 0xF0) == 0xE0 && p[1] && p[2] &&
      (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80) {
    *cp = ((uint32_t)(p[0] & 0x0F) << 12) |
          ((uint32_t)(p[1] & 0x3F) << 6) | (uint32_t)(p[2] & 0x3F);
    *bytes = 3;
    return true;
  }
  if ((p[0] & 0xF8) == 0xF0 && p[1] && p[2] && p[3] &&
      (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80 &&
      (p[3] & 0xC0) == 0x80) {
    *cp = ((uint32_t)(p[0] & 0x07) << 18) |
          ((uint32_t)(p[1] & 0x3F) << 12) |
          ((uint32_t)(p[2] & 0x3F) << 6) | (uint32_t)(p[3] & 0x3F);
    *bytes = 4;
    return true;
  }
  *cp = p[0];
  *bytes = 1;
  return false;
}

static int codepoint_width(uint32_t cp) {
  if (cp == 0)
    return 0;
  if (cp < 32 || (cp >= 0x7F && cp < 0xA0))
    return 0;
  if ((cp >= 0x1100 && cp <= 0x115F) || cp == 0x2329 || cp == 0x232A ||
      (cp >= 0x2E80 && cp <= 0xA4CF) || (cp >= 0xAC00 && cp <= 0xD7A3) ||
      (cp >= 0xF900 && cp <= 0xFAFF) || (cp >= 0xFE10 && cp <= 0xFE19) ||
      (cp >= 0xFE30 && cp <= 0xFE6F) || (cp >= 0xFF00 && cp <= 0xFF60) ||
      (cp >= 0xFFE0 && cp <= 0xFFE6))
    return 2;
  return 1;
}

static int utf8_display_width_n(const char *s, size_t bytes) {
  int width = 0;
  size_t off = 0;
  while (off < bytes && s[off]) {
    uint32_t cp;
    int n;
    utf8_decode_one(s + off, &cp, &n);
    if (off + (size_t)n > bytes)
      break;
    width += codepoint_width(cp);
    off += (size_t)n;
  }
  return width;
}

static size_t utf8_prefix_for_width(const char *s, int max_width, int *used_width) {
  size_t off = 0;
  int width = 0;
  while (s[off]) {
    uint32_t cp;
    int n;
    utf8_decode_one(s + off, &cp, &n);
    int w = codepoint_width(cp);
    if (width + w > max_width)
      break;
    width += w;
    off += (size_t)n;
  }
  if (used_width)
    *used_width = width;
  return off;
}

static void render_session_menu(SessionEntry *entries, int count, int selected,
                                int top, int visible) {
  printf("\033[2K\033[90m  Sessions %d-%d/%d  \xE2\x86\x91\xE2\x86\x93 move  Enter restore  q/Esc cancel\033[0m\n",
         top + 1, top + visible, count);

  for (int row = 0; row < visible; row++) {
    int i = top + row;
    const char *label = entries[i].name[0] ? entries[i].name : entries[i].session_id;
    const char *active = strcmp(entries[i].session_id, session_current_id() ? session_current_id() : "") == 0 ? "*" : " ";
    char size[16];
    char date[32];
    format_size(entries[i].size_bytes, size, sizeof(size));
    format_session_date(entries[i].updated, date, sizeof(date));

    char right[96];
    snprintf(right, sizeof(right), "%s  %4d msg  %8s", date,
             entries[i].message_count, size);

    int rows, cols;
    terminal_size(&rows, &cols);
    (void)rows;
    int right_width = utf8_display_width_n(right, strlen(right));
    int name_width = cols - right_width - 3;
    if (name_width < 1)
      name_width = 1;

    int used_width = 0;
    size_t label_bytes = utf8_prefix_for_width(label, name_width, &used_width);

    printf("\033[2K");
    if (i == selected)
      printf("\033[7m");
    printf("%s %.*s", active, (int)label_bytes, label);
    for (int pad = used_width; pad < name_width; pad++)
      putchar(' ');
    printf("%s", right);
    printf("\033[0m\n");
  }

  printf("\033[2K\033[90m  * current session\033[0m\n");
  printf("\033[%dA", visible + 2);
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
  int visible = session_visible_rows(count);
  int top = 0;

  if (enable_raw_mode() < 0) {
    fprintf(stderr, "Failed to enable raw terminal mode\n");
    session_list_free(entries, count);
    return;
  }

  render_session_menu(entries, count, selected, top, visible);

  int result = -1;
  while (1) {
    char c;
    if (read(STDIN_FILENO, &c, 1) != 1)
      break;

    if (c == '\033') {
      char seq[2];
      if (read_byte_timeout(&seq[0], 80) != 1)
        break;
      if (read_byte_timeout(&seq[1], 80) != 1)
        break;
      if (seq[0] == '[') {
        if (seq[1] == 'A' && selected > 0)
          selected--;
        else if (seq[1] == 'B' && selected < count - 1)
          selected++;
      }
      visible = session_visible_rows(count);
      adjust_session_window(selected, count, visible, &top);
      render_session_menu(entries, count, selected, top, visible);
    } else if (c == '\r' || c == '\n') {
      if (session_restore(entries[selected].session_id, a) == 0)
        result = 0;
      break;
    } else if (c == 'q' || c == 'Q') {
      break;
    }
  }

  disable_raw_mode();

  printf("\033[%dB", visible + 2);
  for (int i = 0; i < visible + 2; i++)
    printf("\033[2K\n");
  printf("\033[%dA", visible + 2);

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
  (void)a;
  if (args[0] != '\0') {
    fprintf(stderr, "Usage: /memory - summarize and save what you learned\n");
    return;
  }

  if (memory_observation_count() == 0) {
    printf("No tool observations to consolidate yet.\n");
    return;
  }

  char err[512];
  int saved = memory_consolidate(err, sizeof(err));
  if (saved < 0) {
    fprintf(stderr, "Memory consolidation failed: %s\n", err);
  } else if (saved == 0) {
    printf("Reviewed observations, but nothing was worth remembering.\n");
  } else {
    printf("Saved %d memory entr%s.\n", saved, saved == 1 ? "y" : "ies");
  }
}

static void cmd_remember(const char *args, Agent *a) {
  if (args[0] != '\0') {
    fprintf(stderr, "Usage: /remember - ask the main agent to save important conversation memory\n");
    return;
  }

  const char *prompt =
      "Review our conversation so far. Use the remember tool to save any "
      "important durable information about this project, the user's preferences, "
      "architecture decisions, build/test workflows, key files, repeated bugs, "
      "or explicit corrections. Be specific and concise. Call remember once for "
      "each distinct high-value memory. Do not save routine command output, "
      "temporary plans, jokes, or anything the user may not want stored. If "
      "nothing is worth saving, say so briefly.";

  const char *reply = agent_chat(a, prompt);
  if (reply) {
    ui_idle();
    ui_print_markdown(reply);
  }
}

static void cmd_help(void) {
  printf("Commands:\n");
  printf("  /model            Interactive model picker\n");
  printf("  /session          Browse and restore saved sessions\n");
  printf("  /session new      Create a new session\n");
  printf("  /session name X   Rename current session\n");
  printf("  /session delete X Delete a session\n");
  printf("  /session restore X Restore a session by id\n");
  printf("  /memory           Consolidate observed facts into project memory\n");
  printf("  /remember         Ask the main agent to save conversation memory\n");
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
  } else if (strncmp(input, "/remember", 9) == 0) {
    const char *args = input + 9;
    while (*args == ' ') args++;
    cmd_remember(args, a);
  } else if (strcmp(input, "/help") == 0) {
    cmd_help();
  } else {
    printf("Unknown command: %s (try /help)\n", input);
  }
  return 1;
}
