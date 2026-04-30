#include "session.h"

#include "config.h"
#include "libs/cJSON.h"
#include "util.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#define SESSION_DIR ".c-agent/sessions"
#define MAX_SESSIONS 256

static void session_dir_path(char *buf, size_t cap) {
  const char *home = getenv("HOME");
  if (!home)
    home = "/tmp";
  snprintf(buf, cap, "%s/%s", home, SESSION_DIR);
}

static int ensure_session_dir(void) {
  char dir[512];
  session_dir_path(dir, sizeof(dir));

  /* mkdir -p: create .c-agent first, then sessions */
  char parent[512];
  const char *home = getenv("HOME");
  snprintf(parent, sizeof(parent), "%s/.c-agent", home ? home : "/tmp");
  mkdir(parent, 0755);
  if (mkdir(dir, 0755) != 0 && errno != EEXIST)
    return -1;
  return 0;
}

/* Find the last user message content from history */
static const char *find_last_user_msg(const MessageList *history) {
  for (int i = history->len - 1; i >= 0; i--) {
    cJSON *msg = cJSON_Parse(history->items[i]);
    if (!msg)
      continue;
    cJSON *role = cJSON_GetObjectItem(msg, "role");
    const char *role_str = cJSON_GetStringValue(role);
    if (role_str && strcmp(role_str, "user") == 0) {
      cJSON *content = cJSON_GetObjectItem(msg, "content");
      const char *text = cJSON_GetStringValue(content);
      if (text) {
        /* need to keep msg alive until caller uses the string */
        /* return a copy */
        static char preview[256];
        snprintf(preview, sizeof(preview), "%s", text);
        cJSON_Delete(msg);
        return preview;
      }
    }
    cJSON_Delete(msg);
  }
  return "";
}

static char current_session_path[600];
static char current_session_created[64];

int session_save(const char *model, const MessageList *history) {
  if (history->len == 0)
    return 0;

  if (ensure_session_dir() < 0)
    return -1;

  /* create the file once per run */
  if (current_session_path[0] == '\0') {
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    char filename[64];
    strftime(filename, sizeof(filename), "%Y%m%d_%H%M%S", t);

    char dir[512];
    session_dir_path(dir, sizeof(dir));
    snprintf(current_session_path, sizeof(current_session_path),
             "%s/%s.json", dir, filename);
    strftime(current_session_created, sizeof(current_session_created),
             "%Y-%m-%dT%H:%M:%S", t);
  }

  /* build JSON */
  cJSON *root = cJSON_CreateObject();
  cJSON_AddStringToObject(root, "model", model);
  cJSON_AddStringToObject(root, "created", current_session_created);

  const char *preview = find_last_user_msg(history);
  cJSON_AddStringToObject(root, "last_user_msg", preview);

  cJSON *msgs = cJSON_CreateArray();
  for (int i = 0; i < history->len; i++) {
    cJSON *item = cJSON_Parse(history->items[i]);
    if (item)
      cJSON_AddItemToArray(msgs, item);
  }
  cJSON_AddItemToObject(root, "messages", msgs);

  char *json = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);

  FILE *f = fopen(current_session_path, "w");
  if (!f) {
    free(json);
    return -1;
  }
  fwrite(json, 1, strlen(json), f);
  fclose(f);
  free(json);
  return 0;
}

int session_load(const char *path, Agent *a) {
  FILE *f = fopen(path, "r");
  if (!f)
    return -1;

  fseek(f, 0, SEEK_END);
  long size = ftell(f);
  fseek(f, 0, SEEK_SET);

  char *buf = xmalloc((size_t)size + 1);
  fread(buf, 1, (size_t)size, f);
  buf[size] = '\0';
  fclose(f);

  cJSON *root = cJSON_Parse(buf);
  free(buf);
  if (!root)
    return -1;

  cJSON *model = cJSON_GetObjectItem(root, "model");
  if (model) {
    const char *m = cJSON_GetStringValue(model);
    if (m)
      snprintf(g_config.model, sizeof(g_config.model), "%s", m);
  }

  cJSON *msgs = cJSON_GetObjectItem(root, "messages");
  if (!msgs || !cJSON_IsArray(msgs)) {
    cJSON_Delete(root);
    return -1;
  }

  agent_clear_history(a);
  MessageList *hist = agent_get_history(a);

  int count = cJSON_GetArraySize(msgs);
  for (int i = 0; i < count; i++) {
    cJSON *item = cJSON_GetArrayItem(msgs, i);
    char *json = cJSON_PrintUnformatted(item);
    if (json)
      msg_list_push(hist, json);
  }

  cJSON_Delete(root);
  return 0;
}

static int cmp_path_desc(const void *a, const void *b) {
  return strcmp(*(const char **)b, *(const char **)a);
}

int session_list(char ***out_paths, char ***out_previews, int *out_count) {
  char dir[512];
  session_dir_path(dir, sizeof(dir));

  DIR *d = opendir(dir);
  if (!d) {
    *out_paths = NULL;
    *out_previews = NULL;
    *out_count = 0;
    return 0;
  }

  char **paths = xmalloc(MAX_SESSIONS * sizeof(char *));
  int count = 0;

  struct dirent *ent;
  while ((ent = readdir(d)) != NULL && count < MAX_SESSIONS) {
    if (ent->d_name[0] == '.')
      continue;
    size_t len = strlen(ent->d_name);
    if (len < 6 || strcmp(ent->d_name + len - 5, ".json") != 0)
      continue;
    paths[count] = xasprintf("%s/%s", dir, ent->d_name);
    count++;
  }
  closedir(d);

  /* sort newest first */
  qsort(paths, (size_t)count, sizeof(char *), cmp_path_desc);

  char **previews = xmalloc((size_t)count * sizeof(char *));
  for (int i = 0; i < count; i++) {
    /* read last_user_msg from file */
    FILE *f = fopen(paths[i], "r");
    previews[i] = xstrdup("");
    if (f) {
      fseek(f, 0, SEEK_END);
      long sz = ftell(f);
      fseek(f, 0, SEEK_SET);
      char *buf = xmalloc((size_t)sz + 1);
      fread(buf, 1, (size_t)sz, f);
      buf[sz] = '\0';
      fclose(f);

      cJSON *root = cJSON_Parse(buf);
      free(buf);
      if (root) {
        cJSON *p = cJSON_GetObjectItem(root, "last_user_msg");
        const char *text = p ? cJSON_GetStringValue(p) : NULL;
        if (text && text[0]) {
          free(previews[i]);
          previews[i] = xstrdup(text);
        }
        cJSON_Delete(root);
      }
    }
  }

  *out_paths = paths;
  *out_previews = previews;
  *out_count = count;
  return 0;
}

void session_list_free(char **paths, char **previews, int count) {
  for (int i = 0; i < count; i++) {
    free(paths[i]);
    free(previews[i]);
  }
  free(paths);
  free(previews);
}

void session_delete_other(const char *keep) {
  if (current_session_path[0] == '\0')
    return;
  if (keep && strcmp(current_session_path, keep) == 0)
    return;
  remove(current_session_path);
  current_session_path[0] = '\0';
}

void session_set_current(const char *path) {
  snprintf(current_session_path, sizeof(current_session_path), "%s", path);
  /* read the created time from the file */
  FILE *f = fopen(path, "r");
  if (f) {
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = xmalloc((size_t)sz + 1);
    fread(buf, 1, (size_t)sz, f);
    buf[sz] = '\0';
    fclose(f);
    cJSON *root = cJSON_Parse(buf);
    free(buf);
    if (root) {
      cJSON *c = cJSON_GetObjectItem(root, "created");
      const char *t = c ? cJSON_GetStringValue(c) : NULL;
      if (t)
        snprintf(current_session_created, sizeof(current_session_created),
                 "%s", t);
      cJSON_Delete(root);
    }
  }
}
