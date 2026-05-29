#include "tools/tools.h"
#include "util.h"

#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define WEB_FETCH_DEFAULT_MAX 50000
#define WEB_FETCH_HARD_MAX 1000000

static bool starts_with(const char *s, const char *prefix) {
    return strncmp(s, prefix, strlen(prefix)) == 0;
}

static bool extract_host(const char *url, char *host, size_t cap) {
    const char *p = strstr(url, "://");
    if (!p)
        return false;
    p += 3;
    const char *end = p;
    while (*end && *end != '/' && *end != ':' && *end != '?' && *end != '#')
        end++;
    size_t len = (size_t)(end - p);
    if (len == 0 || len >= cap)
        return false;
    memcpy(host, p, len);
    host[len] = '\0';
    for (size_t i = 0; host[i]; i++)
        host[i] = (char)tolower((unsigned char)host[i]);
    return true;
}

static bool host_is_blocked(const char *host) {
    if (strcmp(host, "localhost") == 0 || strcmp(host, "::1") == 0 ||
        strcmp(host, "[::1]") == 0)
        return true;
    if (starts_with(host, "127.") || starts_with(host, "10.") ||
        starts_with(host, "192.168.") || starts_with(host, "169.254."))
        return true;
    int a, b;
    if (sscanf(host, "%d.%d", &a, &b) == 2 && a == 172 && b >= 16 && b <= 31)
        return true;
    return false;
}

static bool url_allowed(const char *url, char *err, size_t err_cap) {
    if (!url || !(starts_with(url, "https://") || starts_with(url, "http://"))) {
        snprintf(err, err_cap, "url must start with http:// or https://");
        return false;
    }
    char host[256];
    if (!extract_host(url, host, sizeof(host))) {
        snprintf(err, err_cap, "invalid url host");
        return false;
    }
    if (host_is_blocked(host)) {
        snprintf(err, err_cap, "blocked private/localhost url host: %s", host);
        return false;
    }
    return true;
}

static ToolResult run_curl_fetch(const char *url, int max_bytes) {
    int pipefd[2];
    if (pipe(pipefd) != 0)
        return (ToolResult){.ok = false, .output = xasprintf("pipe: %s", strerror(errno))};

    pid_t pid = fork();
    if (pid < 0) {
        int e = errno;
        close(pipefd[0]); close(pipefd[1]);
        return (ToolResult){.ok = false, .output = xasprintf("fork: %s", strerror(e))};
    }
    if (pid == 0) {
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[1]);
        execlp("curl", "curl", "-L", "--max-time", "20", "--silent", "--show-error",
               "--max-filesize", "1000000", "-D", "-", url, (char *)NULL);
        dprintf(STDERR_FILENO, "exec curl failed: %s\n", strerror(errno));
        _exit(127);
    }

    close(pipefd[1]);
    size_t cap = (size_t)max_bytes + 1;
    char *buf = xmalloc(cap + 128);
    size_t len = 0;
    char tmp[4096];
    ssize_t n;
    bool truncated = false;
    while ((n = read(pipefd[0], tmp, sizeof(tmp))) > 0) {
        size_t take = (size_t)n;
        if (len + take > (size_t)max_bytes) {
            take = (size_t)max_bytes - len;
            truncated = true;
        }
        if (take > 0) {
            memcpy(buf + len, tmp, take);
            len += take;
        }
        if (take < (size_t)n)
            truncated = true;
    }
    close(pipefd[0]);
    int status = 0;
    waitpid(pid, &status, 0);
    buf[len] = '\0';

    bool ok = WIFEXITED(status) && WEXITSTATUS(status) == 0;
    if (!ok) {
        char *out = xasprintf("web_fetch failed for %s\n%s", url, buf);
        free(buf);
        return (ToolResult){.ok = false, .output = out};
    }

    char *out = xasprintf("URL: %s\nMax-Bytes: %d\n\n%s%s", url, max_bytes, buf,
                          truncated ? "\n[truncated]" : "");
    free(buf);
    return (ToolResult){.ok = true, .output = out};
}

static ToolResult tool_web_fetch(cJSON *args) {
    const char *url = cJSON_GetStringValue(cJSON_GetObjectItem(args, "url"));
    char err[256];
    if (!url_allowed(url, err, sizeof(err)))
        return (ToolResult){.ok = false, .output = xstrdup(err)};

    cJSON *max_obj = cJSON_GetObjectItem(args, "max_bytes");
    int max_bytes = (max_obj && cJSON_IsNumber(max_obj)) ? (int)cJSON_GetNumberValue(max_obj) : WEB_FETCH_DEFAULT_MAX;
    if (max_bytes <= 0)
        max_bytes = WEB_FETCH_DEFAULT_MAX;
    if (max_bytes > WEB_FETCH_HARD_MAX)
        max_bytes = WEB_FETCH_HARD_MAX;

    return run_curl_fetch(url, max_bytes);
}

ToolDef web_fetch_def = {
    .name = "web_fetch",
    .desc = "Fetch an external http/https URL using curl. Blocks localhost/private network hosts. Use for explicit URLs, current documentation, or remote skill files.",
    .param_schema =
        "{\"type\":\"object\","
        "\"properties\":{"
        "\"url\":{\"type\":\"string\",\"description\":\"http/https URL to fetch\"},"
        "\"max_bytes\":{\"type\":\"integer\",\"description\":\"optional response cap, default 50000, hard max 1000000\"}"
        "},\"required\":[\"url\"]}",
    .exec = tool_web_fetch,
    .read_only = true,
};
