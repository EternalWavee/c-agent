#include "memory.h"

#include "config.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

/* ── Path helper ─────────────────────────────────────────────────── */

static void memory_path(char *buf, size_t cap) {
    snprintf(buf, cap, "%s/.agent/memory.md", g_config.workdir);
}

/* ── Timestamp ───────────────────────────────────────────────────── */

static void make_iso_time(char *buf, size_t cap) {
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    strftime(buf, cap, "%Y-%m-%d %H:%M", t);
}

/* ── Lifecycle ───────────────────────────────────────────────────── */

int memory_init(void) {
    /* Ensure .agent/ directory exists */
    char agent_dir[PATH_MAX];
    snprintf(agent_dir, sizeof(agent_dir), "%s/.agent", g_config.workdir);
    mkdir(agent_dir, 0755);

    /* Create memory.md if it doesn't exist */
    char path[PATH_MAX];
    memory_path(path, sizeof(path));

    FILE *f = fopen(path, "r");
    if (f) {
        fclose(f);
        return 0;  /* already exists */
    }

    f = fopen(path, "w");
    if (!f) return -1;

    fprintf(f, "# Project Memory\n\n");
    fprintf(f, "This file stores persistent knowledge about this project.\n");
    fprintf(f, "The agent reads it when it needs context from past sessions.\n\n");
    fclose(f);
    return 0;
}

void memory_shutdown(void) {
    /* Nothing to flush — each write is immediate */
}

/* ── Append a memory entry ───────────────────────────────────────── */

int memory_remember(const char *content, const char *category) {
    if (!content || !content[0]) return -1;

    char path[PATH_MAX];
    memory_path(path, sizeof(path));

    FILE *f = fopen(path, "a");
    if (!f) return -1;

    char ts[32];
    make_iso_time(ts, sizeof(ts));

    const char *cat = (category && category[0]) ? category : "general";
    fprintf(f, "- **[%s]** %s: %s\n", cat, ts, content);

    fflush(f);
    fclose(f);
    return 0;
}

/* ── Read the full memory file ───────────────────────────────────── */

char *memory_recall(void) {
    char path[PATH_MAX];
    memory_path(path, sizeof(path));

    FILE *f = fopen(path, "r");
    if (!f) return NULL;

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0) { fclose(f); return NULL; }

    char *buf = xmalloc((size_t)sz + 1);
    fread(buf, 1, (size_t)sz, f);
    buf[sz] = '\0';
    fclose(f);

    return buf;
}

