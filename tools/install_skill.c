#include "tools/tools.h"
#include "config.h"
#include "util.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static bool valid_name(const char *name) {
    if (!name || !name[0]) return false;
    for (const unsigned char *p = (const unsigned char *)name; *p; p++)
        if (!(isalnum(*p) || *p == '_' || *p == '-')) return false;
    return true;
}

static bool is_dir(const char *path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static bool file_exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

static int run_cmd(char *const argv[], char *err, size_t err_cap) {
    int pipefd[2];
    if (pipe(pipefd) != 0) {
        snprintf(err, err_cap, "pipe: %s", strerror(errno));
        return -1;
    }
    pid_t pid = fork();
    if (pid < 0) {
        snprintf(err, err_cap, "fork: %s", strerror(errno));
        close(pipefd[0]); close(pipefd[1]);
        return -1;
    }
    if (pid == 0) {
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[1]);
        execvp(argv[0], argv);
        dprintf(STDERR_FILENO, "exec %s failed: %s\n", argv[0], strerror(errno));
        _exit(127);
    }
    close(pipefd[1]);
    char out[512];
    ssize_t n = read(pipefd[0], out, sizeof(out) - 1);
    if (n < 0) n = 0;
    out[n] = '\0';
    close(pipefd[0]);
    int status = 0;
    waitpid(pid, &status, 0);
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        snprintf(err, err_cap, "%s failed: %s", argv[0], out);
        return -1;
    }
    return 0;
}

static char *read_file_small(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return NULL;
    char *buf = xmalloc(8192);
    size_t n = fread(buf, 1, 8191, f);
    buf[n] = '\0';
    fclose(f);
    return buf;
}

static char *trim(char *s) {
    while (*s && isspace((unsigned char)*s)) s++;
    size_t len = strlen(s);
    while (len > 0 && isspace((unsigned char)s[len - 1])) s[--len] = '\0';
    return s;
}

static char *parse_skill_name(const char *skill_dir) {
    char path[PATH_MAX];
    snprintf(path, sizeof(path), "%s/SKILL.md", skill_dir);
    char *text = read_file_small(path);
    if (!text) return NULL;

    char *save = NULL;
    char *line = strtok_r(text, "\n", &save);
    int lines = 0;
    while (line && lines++ < 80) {
        char *p = trim(line);
        if (strncmp(p, "name", 4) == 0) {
            p += 4;
            while (*p == ':' || *p == '|' || isspace((unsigned char)*p)) p++;
            char *end = strchr(p, '|');
            if (end) *end = '\0';
            p = trim(p);
            if ((*p == '"' || *p == '\'') && p[strlen(p)-1] == *p) {
                p[strlen(p)-1] = '\0';
                p++;
            }
            if (valid_name(p)) {
                char *out = xstrdup(p);
                free(text);
                return out;
            }
        }
        line = strtok_r(NULL, "\n", &save);
    }
    free(text);
    return NULL;
}


static char *target_root_for_scope(const char *scope) {
    if (scope && strcmp(scope, "user") == 0) {
        const char *home = getenv("HOME");
        if (!home || !home[0]) return NULL;
        return xasprintf("%s/.c-agent/skills", home);
    }
    return xasprintf("%s/.agent/skills", g_config.workdir);
}

static int ensure_dir_path(const char *path, char *err, size_t err_cap) {
    char tmp[PATH_MAX];
    snprintf(tmp, sizeof(tmp), "%s", path);
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
    if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
        snprintf(err, err_cap, "mkdir %s: %s", tmp, strerror(errno));
        return -1;
    }
    return 0;
}

static bool parse_github_tree(const char *url, char **repo_out, char **branch_out, char **subpath_out) {
    const char *prefix = "https://github.com/";
    if (strncmp(url, prefix, strlen(prefix)) != 0) return false;
    const char *rest = url + strlen(prefix);
    const char *tree = strstr(rest, "/tree/");
    if (!tree) return false;
    const char *owner_repo_end = tree;
    const char *after_tree = tree + strlen("/tree/");
    const char *slash = strchr(after_tree, '/');
    if (!slash) return false;

    char owner_repo[512];
    size_t or_len = (size_t)(owner_repo_end - rest);
    if (or_len == 0 || or_len >= sizeof(owner_repo)) return false;
    memcpy(owner_repo, rest, or_len); owner_repo[or_len] = '\0';

    char branch[256];
    size_t br_len = (size_t)(slash - after_tree);
    if (br_len == 0 || br_len >= sizeof(branch)) return false;
    memcpy(branch, after_tree, br_len); branch[br_len] = '\0';

    *repo_out = xasprintf("https://github.com/%s.git", owner_repo);
    *branch_out = xstrdup(branch);
    *subpath_out = xstrdup(slash + 1);
    return true;
}

static int clone_repo(const char *repo, const char *branch, const char *dest, char *err, size_t err_cap) {
    char *argv_branch[] = {"git", "clone", "--depth", "1", "--branch", (char *)branch, (char *)repo, (char *)dest, NULL};
    char *argv_plain[] = {"git", "clone", "--depth", "1", (char *)repo, (char *)dest, NULL};
    return run_cmd(branch ? argv_branch : argv_plain, err, err_cap);
}

static ToolResult tool_install_skill(cJSON *args) {
    const char *source = cJSON_GetStringValue(cJSON_GetObjectItem(args, "source"));
    const char *scope = cJSON_GetStringValue(cJSON_GetObjectItem(args, "scope"));
    const char *name_override = cJSON_GetStringValue(cJSON_GetObjectItem(args, "name"));
    cJSON *force_obj = cJSON_GetObjectItem(args, "force");
    bool force = force_obj && cJSON_IsTrue(force_obj);
    char *parsed_name = NULL;
    if (!source || !source[0])
        return (ToolResult){.ok = false, .output = xstrdup("missing 'source' argument")};
    if (scope && strcmp(scope, "project") != 0 && strcmp(scope, "user") != 0)
        return (ToolResult){.ok = false, .output = xstrdup("scope must be 'project' or 'user'")};
    if (name_override && !valid_name(name_override))
        return (ToolResult){.ok = false, .output = xstrdup("invalid skill name override")};

    char err[1024] = "";
    char tmp_base[PATH_MAX];
    snprintf(tmp_base, sizeof(tmp_base), "%s/.agent/tmp/skill-install-%ld", g_config.workdir, (long)time(NULL));
    if (ensure_dir_path(tmp_base, err, sizeof(err)) < 0)
        return (ToolResult){.ok = false, .output = xstrdup(err)};

    char *skill_src = NULL;
    char *repo = NULL, *branch = NULL, *subpath = NULL;
    if (is_dir(source)) {
        skill_src = xstrdup(source);
    } else if (parse_github_tree(source, &repo, &branch, &subpath)) {
        char clone_dir[PATH_MAX];
        snprintf(clone_dir, sizeof(clone_dir), "%s/repo", tmp_base);
        if (clone_repo(repo, branch, clone_dir, err, sizeof(err)) < 0)
            goto fail;
        skill_src = xasprintf("%s/%s", clone_dir, subpath);
    } else if (strstr(source, ".git") || strncmp(source, "https://", 8) == 0 || strncmp(source, "git@", 4) == 0) {
        char clone_dir[PATH_MAX];
        snprintf(clone_dir, sizeof(clone_dir), "%s/repo", tmp_base);
        if (clone_repo(source, NULL, clone_dir, err, sizeof(err)) < 0)
            goto fail;
        skill_src = xstrdup(clone_dir);
    } else {
        snprintf(err, sizeof(err), "unsupported source: use local dir, git repo, or GitHub tree URL");
        goto fail;
    }

    char skill_md[PATH_MAX];
    snprintf(skill_md, sizeof(skill_md), "%s/SKILL.md", skill_src);
    if (!file_exists(skill_md)) {
        snprintf(err, sizeof(err), "SKILL.md not found in %s", skill_src);
        goto fail;
    }

    parsed_name = parse_skill_name(skill_src);
    const char *install_name = name_override && name_override[0] ? name_override : parsed_name;
    if (!valid_name(install_name)) {
        snprintf(err, sizeof(err), "cannot determine safe skill name");
        goto fail;
    }

    char *root = target_root_for_scope(scope ? scope : "project");
    if (!root) {
        snprintf(err, sizeof(err), "cannot determine target skill root");
        goto fail;
    }
    if (ensure_dir_path(root, err, sizeof(err)) < 0)
        goto fail_root;

    char *target = xasprintf("%s/%s", root, install_name);
    if (is_dir(target)) {
        if (!force) {
            snprintf(err, sizeof(err), "skill '%s' already exists; pass force=true to overwrite", install_name);
            free(target);
            goto fail_root;
        }
        char *rm_argv[] = {"rm", "-rf", target, NULL};
        if (run_cmd(rm_argv, err, sizeof(err)) < 0) {
            free(target);
            goto fail_root;
        }
    }
    char *cp_argv[] = {"cp", "-R", skill_src, target, NULL};
    if (run_cmd(cp_argv, err, sizeof(err)) < 0) {
        free(target);
        goto fail_root;
    }

    char *out = xasprintf("Installed skill '%s' to %s (%s scope)", install_name, target,
                          scope && strcmp(scope, "user") == 0 ? "user" : "project");
    free(target); free(root); free(parsed_name); free(skill_src); free(repo); free(branch); free(subpath);
    char *cleanup_argv[] = {"rm", "-rf", tmp_base, NULL};
    run_cmd(cleanup_argv, err, sizeof(err));
    return (ToolResult){.ok = true, .output = out};

fail_root:
    free(root);
fail:
    free(parsed_name); free(skill_src); free(repo); free(branch); free(subpath);
    char *cleanup_argv_fail[] = {"rm", "-rf", tmp_base, NULL};
    run_cmd(cleanup_argv_fail, err, sizeof(err));
    return (ToolResult){.ok = false, .output = xstrdup(err[0] ? err : "install_skill failed")};
}

ToolDef install_skill_def = {
    .name = "install_skill",
    .desc = "Install a skill package from a local directory, git repo URL, or GitHub tree URL. Requires SKILL.md. Installs to .agent/skills by default or ~/.c-agent/skills when scope=user. Does not execute scripts.",
    .param_schema =
        "{\"type\":\"object\","
        "\"properties\":{"
        "\"source\":{\"type\":\"string\",\"description\":\"local directory, git repo URL, or GitHub tree URL\"},"
        "\"scope\":{\"type\":\"string\",\"enum\":[\"project\",\"user\"],\"description\":\"install scope, defaults to project\"},"
        "\"name\":{\"type\":\"string\",\"description\":\"optional safe install name override\"},"
        "\"force\":{\"type\":\"boolean\",\"description\":\"overwrite existing skill if true\"}"
        "},\"required\":[\"source\"]}",
    .exec = tool_install_skill,
    .read_only = false,
};
