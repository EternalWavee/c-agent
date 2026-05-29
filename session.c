#include "session.h"

#include "config.h"
#include "libs/cJSON.h"
#include "memory.h"
#include "util.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

/* ── Module state ────────────────────────────────────────────────── */

static char current_session_id[SESSION_ID_LEN];
static int  new_since_checkpoint;

#define CHECKPOINT_EVERY 10

/* ── Path helpers ────────────────────────────────────────────────── */

static void session_base_dir(char *buf, size_t cap) {
    snprintf(buf, cap, "%s/.agent/sessions", g_config.workdir);
}

static void session_dir(char *buf, size_t cap, const char *id) {
    char base[PATH_MAX];
    session_base_dir(base, sizeof(base));
    snprintf(buf, cap, "%s/%s", base, id);
}

static void session_file(char *buf, size_t cap,
                         const char *id, const char *filename) {
    char dir[PATH_MAX];
    session_dir(dir, sizeof(dir), id);
    snprintf(buf, cap, "%s/%s", dir, filename);
}


static void remove_session_dir_files(const char *id) {
    char dir[PATH_MAX];
    session_dir(dir, sizeof(dir), id);

    char path[PATH_MAX];
    snprintf(path, sizeof(path), "%s/meta.json", dir);
    remove(path);
    snprintf(path, sizeof(path), "%s/checkpoint.json", dir);
    remove(path);
    snprintf(path, sizeof(path), "%s/log.jsonl", dir);
    remove(path);

    rmdir(dir);
}

/* ── Timestamp helpers ───────────────────────────────────────────── */

static void make_timestamp(char *buf, size_t cap, const char *fmt) {
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    strftime(buf, cap, fmt, t);
}

static void make_session_id(char *buf, size_t cap) {
    make_timestamp(buf, cap, "%Y%m%d_%H%M%S");
}

static void make_iso_time(char *buf, size_t cap) {
    make_timestamp(buf, cap, "%Y-%m-%dT%H:%M:%S");
}

/* ── Directory helpers ───────────────────────────────────────────── */

static int ensure_base_dir(void) {
    char agent_dir[PATH_MAX];
    snprintf(agent_dir, sizeof(agent_dir), "%s/.agent", g_config.workdir);
    mkdir(agent_dir, 0755);

    char sessions_dir[PATH_MAX];
    session_base_dir(sessions_dir, sizeof(sessions_dir));
    if (mkdir(sessions_dir, 0755) != 0 && errno != EEXIST)
        return -1;
    return 0;
}

static int ensure_session_dir(const char *id) {
    if (ensure_base_dir() < 0) return -1;
    char dir[PATH_MAX];
    session_dir(dir, sizeof(dir), id);
    if (mkdir(dir, 0755) != 0 && errno != EEXIST)
        return -1;
    return 0;
}

/* ── Atomic file write ───────────────────────────────────────────── */

static int atomic_write(const char *dir, const char *filename,
                        const char *content) {
    char tmp_path[PATH_MAX];
    char final_path[PATH_MAX];
    snprintf(tmp_path, sizeof(tmp_path), "%s/%s.tmp", dir, filename);
    snprintf(final_path, sizeof(final_path), "%s/%s", dir, filename);

    FILE *f = fopen(tmp_path, "w");
    if (!f) return -1;
    fwrite(content, 1, strlen(content), f);
    fflush(f);
    fclose(f);

    if (rename(tmp_path, final_path) != 0) {
        remove(tmp_path);
        return -1;
    }
    return 0;
}

/* ── meta.json read/write ────────────────────────────────────────── */

static int meta_read(const char *id, SessionMeta *meta) {
    char path[PATH_MAX];
    session_file(path, sizeof(path), id, "meta.json");

    FILE *f = fopen(path, "r");
    if (!f) return -1;

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0) { fclose(f); return -1; }

    char *buf = xmalloc((size_t)sz + 1);
    fread(buf, 1, (size_t)sz, f);
    buf[sz] = '\0';
    fclose(f);

    cJSON *root = cJSON_Parse(buf);
    free(buf);
    if (!root) return -1;

    memset(meta, 0, sizeof(*meta));

    cJSON *val;
    val = cJSON_GetObjectItem(root, "id");
    if (val && cJSON_GetStringValue(val))
        snprintf(meta->id, sizeof(meta->id), "%s", cJSON_GetStringValue(val));
    val = cJSON_GetObjectItem(root, "name");
    if (val && cJSON_GetStringValue(val))
        snprintf(meta->name, sizeof(meta->name), "%s", cJSON_GetStringValue(val));
    val = cJSON_GetObjectItem(root, "model");
    if (val && cJSON_GetStringValue(val))
        snprintf(meta->model, sizeof(meta->model), "%s", cJSON_GetStringValue(val));
    val = cJSON_GetObjectItem(root, "created");
    if (val && cJSON_GetStringValue(val))
        snprintf(meta->created, sizeof(meta->created), "%s", cJSON_GetStringValue(val));
    val = cJSON_GetObjectItem(root, "updated");
    if (val && cJSON_GetStringValue(val))
        snprintf(meta->updated, sizeof(meta->updated), "%s", cJSON_GetStringValue(val));
    val = cJSON_GetObjectItem(root, "message_count");
    if (val && cJSON_IsNumber(val))
        meta->message_count = (int)cJSON_GetNumberValue(val);

    cJSON *tags = cJSON_GetObjectItem(root, "tags");
    if (tags && cJSON_IsArray(tags)) {
        int n = cJSON_GetArraySize(tags);
        if (n > SESSION_TAGS_MAX) n = SESSION_TAGS_MAX;
        meta->tag_count = n;
        for (int i = 0; i < n; i++) {
            cJSON *item = cJSON_GetArrayItem(tags, i);
            const char *s = cJSON_GetStringValue(item);
            if (s)
                snprintf(meta->tags[i], SESSION_TAG_LEN, "%s", s);
        }
    }

    cJSON_Delete(root);
    return 0;
}

static int meta_write(const char *id, const SessionMeta *meta) {
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "id", meta->id);
    cJSON_AddStringToObject(root, "name", meta->name);
    cJSON_AddStringToObject(root, "model", meta->model);
    cJSON_AddStringToObject(root, "created", meta->created);
    cJSON_AddStringToObject(root, "updated", meta->updated);
    cJSON_AddNumberToObject(root, "message_count", meta->message_count);

    cJSON *tags = cJSON_CreateArray();
    for (int i = 0; i < meta->tag_count; i++)
        cJSON_AddItemToArray(tags, cJSON_CreateString(meta->tags[i]));
    cJSON_AddItemToObject(root, "tags", tags);

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    char dir[PATH_MAX];
    session_dir(dir, sizeof(dir), id);
    int rc = atomic_write(dir, "meta.json", json);
    free(json);
    return rc;
}

/* ── Find latest session ─────────────────────────────────────────── */

static int find_latest_session(char *id_out, size_t cap) {
    char base[PATH_MAX];
    session_base_dir(base, sizeof(base));
    DIR *d = opendir(base);
    if (!d) return -1;

    char best[SESSION_ID_LEN] = "";
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (ent->d_name[0] == '.') continue;
        if (strlen(ent->d_name) != 15) continue;
        if (ent->d_name[8] != '_') continue;
        if (strcmp(ent->d_name, best) > 0)
            snprintf(best, sizeof(best), "%s", ent->d_name);
    }
    closedir(d);

    if (best[0] == '\0') return -1;
    snprintf(id_out, cap, "%s", best);
    return 0;
}

/* ── Load session into agent (shared by recovery and restore) ────── */

static int load_session_into_agent(const char *id, Agent *a) {
    agent_clear_history(a);

    /* 1. Load checkpoint */
    char path[PATH_MAX];
    session_file(path, sizeof(path), id, "checkpoint.json");
    FILE *f = fopen(path, "r");
    if (f) {
        fseek(f, 0, SEEK_END);
        long sz = ftell(f);
        fseek(f, 0, SEEK_SET);
        if (sz > 0) {
            char *buf = xmalloc((size_t)sz + 1);
            fread(buf, 1, (size_t)sz, f);
            buf[sz] = '\0';
            fclose(f);

            cJSON *arr = cJSON_Parse(buf);
            free(buf);
            if (arr && cJSON_IsArray(arr)) {
                int n = cJSON_GetArraySize(arr);
                for (int i = 0; i < n; i++) {
                    cJSON *item = cJSON_GetArrayItem(arr, i);
                    char *json = cJSON_PrintUnformatted(item);
                    if (json) agent_push_message(a, json);
                }
            }
            if (arr) cJSON_Delete(arr);
        } else {
            fclose(f);
        }
    }

    /* 2. Replay log.jsonl */
    session_file(path, sizeof(path), id, "log.jsonl");
    f = fopen(path, "r");
    if (f) {
        char line[65536];
        while (fgets(line, sizeof(line), f)) {
            size_t len = strlen(line);
            while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r'))
                line[--len] = '\0';
            if (len == 0) continue;

            cJSON *entry = cJSON_Parse(line);
            if (!entry) continue;  /* partial/corrupt line — skip */

            /* Lines with "type":"meta" are metadata, skip for now */
            cJSON *type = cJSON_GetObjectItem(entry, "type");
            if (type && cJSON_GetStringValue(type) &&
                strcmp(cJSON_GetStringValue(type), "meta") == 0) {
                cJSON_Delete(entry);
                continue;
            }

            /* Regular message — strip "type" field if present, push as-is */
            if (type)
                cJSON_DeleteItemFromObject(entry, "type");
            char *json = cJSON_PrintUnformatted(entry);
            if (json) agent_push_message(a, json);
            cJSON_Delete(entry);
        }
        fclose(f);
    }

    return 0;
}

/* ── Migration from old flat-file format ─────────────────────────── */

static void migrate_old_sessions(void) {
    char base[PATH_MAX];
    session_base_dir(base, sizeof(base));
    DIR *d = opendir(base);
    if (!d) return;

    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        size_t len = strlen(ent->d_name);
        if (len < 6 || strcmp(ent->d_name + len - 5, ".json") != 0)
            continue;

        char id[SESSION_ID_LEN];
        snprintf(id, sizeof(id), "%.*s", (int)(len - 5), ent->d_name);

        /* Skip if new-format directory already exists */
        char new_dir[PATH_MAX];
        session_dir(new_dir, sizeof(new_dir), id);
        struct stat st;
        if (stat(new_dir, &st) == 0 && S_ISDIR(st.st_mode))
            continue;

        /* Read old JSON file */
        char old_path[PATH_MAX];
        snprintf(old_path, sizeof(old_path), "%s/%s", base, ent->d_name);

        FILE *f = fopen(old_path, "r");
        if (!f) continue;
        fseek(f, 0, SEEK_END);
        long sz = ftell(f);
        fseek(f, 0, SEEK_SET);
        char *buf = xmalloc((size_t)sz + 1);
        fread(buf, 1, (size_t)sz, f);
        buf[sz] = '\0';
        fclose(f);

        cJSON *root = cJSON_Parse(buf);
        free(buf);
        if (!root) continue;

        /* Create new directory */
        mkdir(new_dir, 0755);

        /* Write checkpoint.json from messages array */
        cJSON *msgs = cJSON_GetObjectItem(root, "messages");
        if (msgs && cJSON_IsArray(msgs)) {
            char *cp_json = cJSON_PrintUnformatted(msgs);
            if (cp_json) {
                atomic_write(new_dir, "checkpoint.json", cp_json);
                free(cp_json);
            }
        }

        /* Write meta.json */
        SessionMeta meta;
        memset(&meta, 0, sizeof(meta));
        snprintf(meta.id, sizeof(meta.id), "%s", id);
        cJSON *model = cJSON_GetObjectItem(root, "model");
        if (model && cJSON_GetStringValue(model))
            snprintf(meta.model, sizeof(meta.model), "%s",
                     cJSON_GetStringValue(model));
        cJSON *created = cJSON_GetObjectItem(root, "created");
        if (created && cJSON_GetStringValue(created))
            snprintf(meta.created, sizeof(meta.created), "%s",
                     cJSON_GetStringValue(created));
        snprintf(meta.updated, sizeof(meta.updated), "%s", meta.created);
        if (msgs && cJSON_IsArray(msgs))
            meta.message_count = cJSON_GetArraySize(msgs);
        meta_write(id, &meta);

        /* Create empty log */
        char log_path[PATH_MAX];
        session_file(log_path, sizeof(log_path), id, "log.jsonl");
        FILE *lf = fopen(log_path, "w");
        if (lf) fclose(lf);

        cJSON_Delete(root);

        /* Remove old file */
        remove(old_path);
    }
    closedir(d);
}

/* ── Get preview (last user message) from checkpoint ─────────────── */

static void get_preview(const char *id, char *buf, size_t cap) {
    buf[0] = '\0';

    char path[PATH_MAX];
    session_file(path, sizeof(path), id, "checkpoint.json");
    FILE *f = fopen(path, "r");
    if (!f) return;

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0) { fclose(f); return; }

    char *content = xmalloc((size_t)sz + 1);
    fread(content, 1, (size_t)sz, f);
    content[sz] = '\0';
    fclose(f);

    cJSON *arr = cJSON_Parse(content);
    free(content);
    if (!arr || !cJSON_IsArray(arr)) { if (arr) cJSON_Delete(arr); return; }

    /* Scan backwards for last user message */
    int n = cJSON_GetArraySize(arr);
    for (int i = n - 1; i >= 0; i--) {
        cJSON *item = cJSON_GetArrayItem(arr, i);
        cJSON *role = cJSON_GetObjectItem(item, "role");
        const char *role_str = cJSON_GetStringValue(role);
        if (role_str && strcmp(role_str, "user") == 0) {
            cJSON *content_val = cJSON_GetObjectItem(item, "content");
            const char *text = cJSON_GetStringValue(content_val);
            if (text) {
                snprintf(buf, cap, "%s", text);
                break;
            }
        }
    }
    cJSON_Delete(arr);
}

/* ── Public API: Lifecycle ───────────────────────────────────────── */

int session_startup_recovery(Agent *a) {
    ensure_base_dir();
    migrate_old_sessions();

    char id[SESSION_ID_LEN];
    if (find_latest_session(id, sizeof(id)) < 0) {
        /* No sessions exist — create a fresh one */
        return session_new(a);
    }

    if (ensure_session_dir(id) < 0)
        return -1;

    if (load_session_into_agent(id, a) < 0)
        return -1;

    snprintf(current_session_id, sizeof(current_session_id), "%s", id);
    new_since_checkpoint = 0;
    return 0;
}

int session_new(Agent *a) {
    if (current_session_id[0] != '\0') {
        if (agent_history_count(a) > 0)
            session_checkpoint(a);
        else
            remove_session_dir_files(current_session_id);
    }

    char id[SESSION_ID_LEN];
    make_session_id(id, sizeof(id));

    if (ensure_session_dir(id) < 0)
        return -1;

    /* Write meta.json */
    SessionMeta meta;
    memset(&meta, 0, sizeof(meta));
    snprintf(meta.id, sizeof(meta.id), "%s", id);
    snprintf(meta.model, sizeof(meta.model), "%s", g_config.model);
    make_iso_time(meta.created, sizeof(meta.created));
    snprintf(meta.updated, sizeof(meta.updated), "%s", meta.created);
    meta_write(id, &meta);

    /* Write empty checkpoint */
    char dir[PATH_MAX];
    session_dir(dir, sizeof(dir), id);
    atomic_write(dir, "checkpoint.json", "[]");

    /* Create empty log */
    char log_path[PATH_MAX];
    session_file(log_path, sizeof(log_path), id, "log.jsonl");
    FILE *f = fopen(log_path, "w");
    if (f) fclose(f);

    agent_clear_history(a);
    snprintf(current_session_id, sizeof(current_session_id), "%s", id);
    new_since_checkpoint = 0;
    return 0;
}

int session_append(Agent *a, int from) {
    int count = agent_history_count(a);
    if (count <= from)
        return 0;

    if (current_session_id[0] == '\0')
        return -1;

    char log_path[PATH_MAX];
    session_file(log_path, sizeof(log_path), current_session_id, "log.jsonl");
    FILE *f = fopen(log_path, "a");
    if (!f) return -1;

    for (int i = from; i < count; i++) {
        const char *msg = agent_get_message(a, i);
        if (msg)
            fprintf(f, "%s\n", msg);
    }
    fflush(f);
    fclose(f);

    /* Update meta */
    SessionMeta meta;
    if (meta_read(current_session_id, &meta) == 0) {
        meta.message_count = count;
        make_iso_time(meta.updated, sizeof(meta.updated));

        /* Auto-name: use last user message as session name */
        for (int i = count - 1; i >= from; i--) {
            const char *msg = agent_get_message(a, i);
            if (!msg) continue;
            cJSON *obj = cJSON_Parse(msg);
            if (!obj) continue;
            cJSON *role = cJSON_GetObjectItem(obj, "role");
            const char *role_str = cJSON_GetStringValue(role);
            if (role_str && strcmp(role_str, "user") == 0) {
                cJSON *content = cJSON_GetObjectItem(obj, "content");
                const char *text = cJSON_GetStringValue(content);
                if (text && text[0])
                    snprintf(meta.name, sizeof(meta.name), "%s", text);
                cJSON_Delete(obj);
                break;
            }
            cJSON_Delete(obj);
        }

        meta_write(current_session_id, &meta);
    }

    new_since_checkpoint += (count - from);
    if (new_since_checkpoint >= CHECKPOINT_EVERY) {
        session_checkpoint(a);
    }

    return 0;
}

int session_checkpoint(Agent *a) {
    if (current_session_id[0] == '\0') return -1;

    char dir[PATH_MAX];
    session_dir(dir, sizeof(dir), current_session_id);

    /* Build checkpoint JSON array */
    cJSON *arr = cJSON_CreateArray();
    int count = agent_history_count(a);
    for (int i = 0; i < count; i++) {
        const char *msg = agent_get_message(a, i);
        if (msg) {
            cJSON *item = cJSON_Parse(msg);
            if (item) cJSON_AddItemToArray(arr, item);
        }
    }

    char *json = cJSON_PrintUnformatted(arr);
    cJSON_Delete(arr);

    int rc = atomic_write(dir, "checkpoint.json", json);
    free(json);

    if (rc == 0) {
        /* Truncate log */
        char log_path[PATH_MAX];
        session_file(log_path, sizeof(log_path),
                     current_session_id, "log.jsonl");
        FILE *f = fopen(log_path, "w");
        if (f) fclose(f);
    }

    new_since_checkpoint = 0;
    return rc;
}

int session_restore(const char *session_id, Agent *a) {
    if (strcmp(session_id, current_session_id) == 0)
        return 0;

    char dir[PATH_MAX];
    session_dir(dir, sizeof(dir), session_id);
    struct stat st;
    if (stat(dir, &st) != 0 || !S_ISDIR(st.st_mode))
        return -1;

    if (current_session_id[0] != '\0') {
        if (agent_history_count(a) > 0)
            session_checkpoint(a);
        else
            remove_session_dir_files(current_session_id);
    }

    /* Load target session */
    if (load_session_into_agent(session_id, a) < 0)
        return -1;

    snprintf(current_session_id, sizeof(current_session_id), "%s", session_id);
    new_since_checkpoint = 0;

    /* Touch updated timestamp */
    SessionMeta meta;
    if (meta_read(session_id, &meta) == 0) {
        make_iso_time(meta.updated, sizeof(meta.updated));
        meta_write(session_id, &meta);
    }

    return 0;
}

void session_shutdown(Agent *a) {
    if (current_session_id[0] != '\0') {
        if (agent_history_count(a) > 0) {
            session_checkpoint(a);
        } else {
            remove_session_dir_files(current_session_id);
        }
    }

    /* Consolidate short-term observations into long-term memory */
    char err[256];
    if (memory_consolidate(err, sizeof(err)) < 0)
        fprintf(stderr, "[memory] consolidate failed: %s\n", err);

    current_session_id[0] = '\0';
}

/* ── Public API: Metadata ────────────────────────────────────────── */

int session_rename(const char *new_name) {
    if (current_session_id[0] == '\0') return -1;

    SessionMeta meta;
    if (meta_read(current_session_id, &meta) != 0)
        return -1;

    snprintf(meta.name, sizeof(meta.name), "%s", new_name);
    return meta_write(current_session_id, &meta);
}

int session_add_tag(const char *tag) {
    if (current_session_id[0] == '\0') return -1;

    SessionMeta meta;
    if (meta_read(current_session_id, &meta) != 0)
        return -1;

    if (meta.tag_count >= SESSION_TAGS_MAX)
        return -1;

    snprintf(meta.tags[meta.tag_count], SESSION_TAG_LEN, "%s", tag);
    meta.tag_count++;
    return meta_write(current_session_id, &meta);
}

int session_delete(const char *session_id) {
    if (strcmp(session_id, current_session_id) == 0)
        return -1;  /* refuse to delete active session */

    char dir[PATH_MAX];
    session_dir(dir, sizeof(dir), session_id);

    char path[PATH_MAX];
    snprintf(path, sizeof(path), "%s/meta.json", dir);
    remove(path);
    snprintf(path, sizeof(path), "%s/checkpoint.json", dir);
    remove(path);
    snprintf(path, sizeof(path), "%s/log.jsonl", dir);
    remove(path);

    return rmdir(dir);
}

/* ── Public API: Listing ─────────────────────────────────────────── */

static int cmp_entry_desc(const void *a, const void *b) {
    const SessionEntry *ea = a;
    const SessionEntry *eb = b;
    return strcmp(eb->updated, ea->updated);
}

static long file_size_or_zero(const char *path) {
    struct stat st;
    if (stat(path, &st) == 0 && S_ISREG(st.st_mode))
        return (long)st.st_size;
    return 0;
}

static long session_storage_size(const char *id) {
    char path[PATH_MAX];
    long total = 0;
    session_file(path, sizeof(path), id, "meta.json");
    total += file_size_or_zero(path);
    session_file(path, sizeof(path), id, "checkpoint.json");
    total += file_size_or_zero(path);
    session_file(path, sizeof(path), id, "log.jsonl");
    total += file_size_or_zero(path);
    return total;
}

int session_list(SessionEntry **out) {
    char base[PATH_MAX];
    session_base_dir(base, sizeof(base));
    DIR *d = opendir(base);
    if (!d) { *out = NULL; return 0; }

    int cap = 64;
    SessionEntry *entries = xmalloc((size_t)cap * sizeof(SessionEntry));
    int count = 0;

    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (ent->d_name[0] == '.') continue;
        if (strlen(ent->d_name) != 15 || ent->d_name[8] != '_') continue;

        SessionMeta meta;
        if (meta_read(ent->d_name, &meta) != 0) continue;
        if (meta.message_count <= 0) continue;

        if (count >= cap) {
            cap *= 2;
            entries = xrealloc(entries, (size_t)cap * sizeof(SessionEntry));
        }

        SessionEntry *e = &entries[count++];
        snprintf(e->session_id, sizeof(e->session_id), "%s", meta.id);
        snprintf(e->name, sizeof(e->name), "%s", meta.name);
        snprintf(e->model, sizeof(e->model), "%s", meta.model);
        snprintf(e->created, sizeof(e->created), "%s", meta.created);
        snprintf(e->updated, sizeof(e->updated), "%s", meta.updated);
        e->message_count = meta.message_count;
        e->size_bytes = session_storage_size(ent->d_name);

        get_preview(ent->d_name, e->preview, sizeof(e->preview));
    }
    closedir(d);

    qsort(entries, (size_t)count, sizeof(SessionEntry), cmp_entry_desc);

    *out = entries;
    return count;
}

void session_list_free(SessionEntry *list, int count) {
    (void)count;
    free(list);
}

const char *session_current_id(void) {
    return current_session_id[0] ? current_session_id : NULL;
}
