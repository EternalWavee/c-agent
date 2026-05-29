#include "skills.h"

#include "config.h"
#include "util.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define META_VALUE_MAX 1024

typedef struct {
    char name[128];
    char description[META_VALUE_MAX];
    char allowed_tools[512];
    char scope[16];
} SkillMeta;

static void project_skills_dir(char *buf, size_t cap) {
    snprintf(buf, cap, "%s/.agent/skills", g_config.workdir);
}

static void user_skills_dir(char *buf, size_t cap) {
    const char *home = getenv("HOME");
    if (!home || !home[0])
        buf[0] = '\0';
    else
        snprintf(buf, cap, "%s/.c-agent/skills", home);
}

static void skill_path_in_dir(const char *dir, const char *name, char *buf, size_t cap) {
    snprintf(buf, cap, "%s/%s/SKILL.md", dir, name);
}

static bool valid_skill_name(const char *name) {
    if (!name || !name[0])
        return false;
    for (const unsigned char *p = (const unsigned char *)name; *p; p++) {
        if (!(isalnum(*p) || *p == '_' || *p == '-'))
            return false;
    }
    return true;
}

static char *read_text_file(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0) {
        fclose(f);
        return xstrdup("");
    }
    char *buf = xmalloc((size_t)sz + 1);
    size_t n = fread(buf, 1, (size_t)sz, f);
    buf[n] = '\0';
    fclose(f);
    return buf;
}

static char *trim_in_place(char *s) {
    while (*s && isspace((unsigned char)*s))
        s++;
    size_t len = strlen(s);
    while (len > 0 && isspace((unsigned char)s[len - 1]))
        s[--len] = '\0';
    return s;
}

static void copy_meta_value(char *dst, size_t cap, const char *value) {
    if (!value)
        return;
    char tmp[META_VALUE_MAX];
    snprintf(tmp, sizeof(tmp), "%s", value);
    char *v = trim_in_place(tmp);
    size_t len = strlen(v);
    if (len >= 2 && ((v[0] == '"' && v[len - 1] == '"') ||
                     (v[0] == '\'' && v[len - 1] == '\''))) {
        v[len - 1] = '\0';
        v++;
    }
    snprintf(dst, cap, "%s", v);
}

static void set_meta_field(SkillMeta *meta, const char *key, const char *value) {
    if (!key || !value)
        return;
    if (strcmp(key, "name") == 0)
        copy_meta_value(meta->name, sizeof(meta->name), value);
    else if (strcmp(key, "description") == 0)
        copy_meta_value(meta->description, sizeof(meta->description), value);
    else if (strcmp(key, "allowed-tools") == 0 || strcmp(key, "allowed_tools") == 0)
        copy_meta_value(meta->allowed_tools, sizeof(meta->allowed_tools), value);
}

static void parse_key_value_line(SkillMeta *meta, char *line) {
    char *p = trim_in_place(line);
    if (!*p || *p == '#')
        return;

    if (*p == '|') {
        char *cells[3] = {0};
        int count = 0;
        char *cur = p + 1;
        while (count < 3) {
            char *bar = strchr(cur, '|');
            if (!bar)
                break;
            *bar = '\0';
            cells[count++] = trim_in_place(cur);
            cur = bar + 1;
        }
        if (count >= 2 && strcmp(cells[0], "---") != 0 && strcmp(cells[0], "field") != 0)
            set_meta_field(meta, cells[0], cells[1]);
        return;
    }

    char *sep = strchr(p, ':');
    if (!sep) {
        sep = p;
        while (*sep && !isspace((unsigned char)*sep))
            sep++;
        if (!*sep)
            return;
    }

    *sep = '\0';
    char *key = trim_in_place(p);
    char *value = trim_in_place(sep + 1);
    set_meta_field(meta, key, value);
}

static SkillMeta parse_skill_meta(const char *skill_dir_name, const char *text) {
    SkillMeta meta;
    memset(&meta, 0, sizeof(meta));
    snprintf(meta.name, sizeof(meta.name), "%s", skill_dir_name);

    char *copy = xstrdup(text ? text : "");
    char *save = NULL;
    char *line = strtok_r(copy, "\n", &save);
    int line_no = 0;
    bool in_frontmatter = false;
    bool saw_frontmatter = false;

    while (line && line_no < 80) {
        char *work = trim_in_place(line);
        if (line_no == 0 && strcmp(work, "---") == 0) {
            in_frontmatter = true;
            saw_frontmatter = true;
            line = strtok_r(NULL, "\n", &save);
            line_no++;
            continue;
        }
        if (in_frontmatter && strcmp(work, "---") == 0)
            break;
        if (!saw_frontmatter || in_frontmatter)
            parse_key_value_line(&meta, work);
        line = strtok_r(NULL, "\n", &save);
        line_no++;
    }

    free(copy);
    return meta;
}

static int append_str(char **buf, size_t *len, size_t *cap, const char *s) {
    size_t need = strlen(s);
    while (*len + need + 1 > *cap) {
        *cap *= 2;
        *buf = xrealloc(*buf, *cap);
    }
    memcpy(*buf + *len, s, need);
    *len += need;
    (*buf)[*len] = '\0';
    return 0;
}

static void meta_list_upsert(SkillMeta **items, int *count, int *cap,
                             SkillMeta meta) {
    for (int i = 0; i < *count; i++) {
        if (strcmp((*items)[i].name, meta.name) == 0) {
            (*items)[i] = meta;
            return;
        }
    }
    if (*count >= *cap) {
        *cap *= 2;
        *items = xrealloc(*items, (size_t)*cap * sizeof(**items));
    }
    (*items)[(*count)++] = meta;
}

static void collect_skills_from_dir(const char *dir, const char *scope,
                                    SkillMeta **items, int *count, int *cap) {
    if (!dir || !dir[0])
        return;

    DIR *d = opendir(dir);
    if (!d)
        return;

    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (ent->d_name[0] == '.' || !valid_skill_name(ent->d_name))
            continue;

        char path[PATH_MAX];
        skill_path_in_dir(dir, ent->d_name, path, sizeof(path));
        char *text = read_text_file(path);
        if (!text)
            continue;

        SkillMeta meta = parse_skill_meta(ent->d_name, text);
        free(text);
        if (!meta.description[0])
            snprintf(meta.description, sizeof(meta.description), "No description provided.");
        snprintf(meta.scope, sizeof(meta.scope), "%s", scope);
        meta_list_upsert(items, count, cap, meta);
    }
    closedir(d);
}

int skills_init(void) {
    char agent_dir[PATH_MAX];
    snprintf(agent_dir, sizeof(agent_dir), "%s/.agent", g_config.workdir);
    if (mkdir(agent_dir, 0755) != 0 && errno != EEXIST)
        return -1;

    char project_dir[PATH_MAX];
    project_skills_dir(project_dir, sizeof(project_dir));
    if (mkdir(project_dir, 0755) != 0 && errno != EEXIST)
        return -1;

    char user_dir[PATH_MAX];
    user_skills_dir(user_dir, sizeof(user_dir));
    if (user_dir[0]) {
        char user_base[PATH_MAX];
        snprintf(user_base, sizeof(user_base), "%s/.c-agent", getenv("HOME"));
        if (mkdir(user_base, 0755) != 0 && errno != EEXIST)
            return -1;
        if (mkdir(user_dir, 0755) != 0 && errno != EEXIST)
            return -1;
    }
    return 0;
}

char *skills_build_prompt(void) {
    int cap_items = 16;
    int count = 0;
    SkillMeta *items = xmalloc((size_t)cap_items * sizeof(*items));

    char user_dir[PATH_MAX];
    char project_dir[PATH_MAX];
    user_skills_dir(user_dir, sizeof(user_dir));
    project_skills_dir(project_dir, sizeof(project_dir));

    collect_skills_from_dir(user_dir, "user", &items, &count, &cap_items);
    collect_skills_from_dir(project_dir, "project", &items, &count, &cap_items);

    if (count == 0) {
        free(items);
        return xstrdup("");
    }

    size_t out_cap = 4096, len = 0;
    char *buf = xmalloc(out_cap);
    buf[0] = '\0';
    append_str(&buf, &len, &out_cap,
               "## Available Skills\n"
               "Load a skill with load_skill when it is relevant. Startup only includes this manifest; load_skill returns the full SKILL.md.\n"
               "Skill scopes: project skills override user skills with the same name.\n");

    for (int i = 0; i < count; i++) {
        char *line;
        if (items[i].allowed_tools[0])
            line = xasprintf("- %s [%s]: %s (allowed-tools: %s)\n",
                             items[i].name, items[i].scope, items[i].description,
                             items[i].allowed_tools);
        else
            line = xasprintf("- %s [%s]: %s\n",
                             items[i].name, items[i].scope, items[i].description);
        append_str(&buf, &len, &out_cap, line);
        free(line);
    }

    free(items);
    return buf;
}

char *skills_load(const char *name, char *err, size_t err_cap) {
    if (!valid_skill_name(name)) {
        snprintf(err, err_cap, "invalid skill name");
        return NULL;
    }

    char project_dir[PATH_MAX];
    char user_dir[PATH_MAX];
    char path[PATH_MAX];

    project_skills_dir(project_dir, sizeof(project_dir));
    skill_path_in_dir(project_dir, name, path, sizeof(path));
    char *text = read_text_file(path);
    if (text)
        return text;

    user_skills_dir(user_dir, sizeof(user_dir));
    if (user_dir[0]) {
        skill_path_in_dir(user_dir, name, path, sizeof(path));
        text = read_text_file(path);
        if (text)
            return text;
    }

    snprintf(err, err_cap,
             "cannot open skill '%s' in .agent/skills or ~/.c-agent/skills",
             name);
    return NULL;
}
