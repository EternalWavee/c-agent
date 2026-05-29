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

#define SEARCH_MAX_HTML 200000

static bool is_unreserved(unsigned char c) {
    return isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~';
}

static char *url_encode(const char *s) {
    if (!s) return xstrdup("");
    size_t len = strlen(s);
    char *out = xmalloc(len * 3 + 1);
    size_t wi = 0;
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)s[i];
        if (is_unreserved(c)) {
            out[wi++] = (char)c;
        } else if (c == ' ') {
            out[wi++] = '+';
        } else {
            static const char hex[] = "0123456789ABCDEF";
            out[wi++] = '%';
            out[wi++] = hex[c >> 4];
            out[wi++] = hex[c & 15];
        }
    }
    out[wi] = '\0';
    return out;
}

static char *run_curl(const char *url, char *err, size_t err_cap) {
    int pipefd[2];
    if (pipe(pipefd) != 0) {
        snprintf(err, err_cap, "pipe: %s", strerror(errno));
        return NULL;
    }

    pid_t pid = fork();
    if (pid < 0) {
        int e = errno;
        close(pipefd[0]); close(pipefd[1]);
        snprintf(err, err_cap, "fork: %s", strerror(e));
        return NULL;
    }
    if (pid == 0) {
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[1]);
        execlp("curl", "curl", "-L", "--max-time", "15", "--silent", "--show-error",
               "--max-filesize", "300000", "-A",
               "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 Chrome/120 Safari/537.36",
               url, (char *)NULL);
        dprintf(STDERR_FILENO, "exec curl failed: %s\n", strerror(errno));
        _exit(127);
    }

    close(pipefd[1]);
    char *buf = xmalloc(SEARCH_MAX_HTML + 1);
    size_t len = 0;
    char tmp[4096];
    ssize_t n;
    while ((n = read(pipefd[0], tmp, sizeof(tmp))) > 0) {
        size_t take = (size_t)n;
        if (len + take > SEARCH_MAX_HTML)
            take = SEARCH_MAX_HTML - len;
        if (take > 0) {
            memcpy(buf + len, tmp, take);
            len += take;
        }
        if (len >= SEARCH_MAX_HTML)
            break;
    }
    close(pipefd[0]);
    int status = 0;
    waitpid(pid, &status, 0);
    buf[len] = '\0';

    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        snprintf(err, err_cap, "curl failed: %.300s", buf);
        free(buf);
        return NULL;
    }
    return buf;
}

static void html_entity_decode(char *s) {
    char *r = s, *w = s;
    while (*r) {
        if (strncmp(r, "&amp;", 5) == 0) { *w++ = '&'; r += 5; }
        else if (strncmp(r, "&lt;", 4) == 0) { *w++ = '<'; r += 4; }
        else if (strncmp(r, "&gt;", 4) == 0) { *w++ = '>'; r += 4; }
        else if (strncmp(r, "&quot;", 6) == 0) { *w++ = '"'; r += 6; }
        else if (strncmp(r, "&#39;", 5) == 0) { *w++ = '\''; r += 5; }
        else { *w++ = *r++; }
    }
    *w = '\0';
}

static char *extract_xml_tag(const char *start, const char *end, const char *tag) {
    char open[64], close[64];
    snprintf(open, sizeof(open), "<%s>", tag);
    snprintf(close, sizeof(close), "</%s>", tag);
    const char *a = strstr(start, open);
    if (!a || a >= end) return xstrdup("");
    a += strlen(open);
    const char *b = strstr(a, close);
    if (!b || b > end) return xstrdup("");
    char *out = xmalloc((size_t)(b - a) + 1);
    memcpy(out, a, (size_t)(b - a));
    out[b - a] = '\0';
    html_entity_decode(out);
    return out;
}

static char *parse_results(const char *html, const char *search_url, int max_results) {
    size_t cap = 8192;
    char *out = xmalloc(cap);
    size_t len = 0;
    len += (size_t)snprintf(out + len, cap - len, "Search URL: %s\n\n", search_url);

    int found = 0;
    const char *p = html;
    while (found < max_results && (p = strstr(p, "<item>")) != NULL) {
        const char *item_start = p + strlen("<item>");
        const char *item_end = strstr(item_start, "</item>");
        if (!item_end) break;

        char *title = extract_xml_tag(item_start, item_end, "title");
        char *url = extract_xml_tag(item_start, item_end, "link");
        char *snippet = extract_xml_tag(item_start, item_end, "description");

        if (url[0]) {
            size_t need = strlen(title) + strlen(url) + strlen(snippet) + 128;
            if (len + need >= cap) {
                while (len + need >= cap) cap *= 2;
                out = xrealloc(out, cap);
            }
            len += (size_t)snprintf(out + len, cap - len,
                                    "%d. %s\n   %s\n   %s\n\n",
                                    found + 1, title[0] ? title : "(untitled)",
                                    url, snippet);
            found++;
        }

        free(title); free(url); free(snippet);
        p = item_end + strlen("</item>");
    }

    if (found == 0) {
        len += (size_t)snprintf(out + len, cap - len,
                                "No parseable Bing RSS results found. Try a different query or use a known URL with web_fetch/agent-browser.\n");
    }
    return out;
}

static ToolResult tool_web_search(cJSON *args) {
    const char *query = cJSON_GetStringValue(cJSON_GetObjectItem(args, "query"));
    if (!query || !query[0])
        return (ToolResult){.ok = false, .output = xstrdup("missing query")};

    cJSON *max_obj = cJSON_GetObjectItem(args, "max_results");
    int max_results = (max_obj && cJSON_IsNumber(max_obj)) ? (int)cJSON_GetNumberValue(max_obj) : 5;
    if (max_results <= 0) max_results = 5;
    if (max_results > 10) max_results = 10;

    char *q = url_encode(query);
    char *url = xasprintf("https://www.bing.com/search?format=rss&mkt=zh-CN&setlang=zh-CN&q=%s", q);
    free(q);

    char err[512] = "";
    char *html = run_curl(url, err, sizeof(err));
    if (!html) {
        char *out = xasprintf("web_search failed for %s: %s", url, err);
        free(url);
        return (ToolResult){.ok = false, .output = out};
    }

    char *out = parse_results(html, url, max_results);
    free(html);
    free(url);
    return (ToolResult){.ok = true, .output = out};
}

ToolDef web_search_def = {
    .name = "web_search",
    .desc = "Search the web using Bing RSS results. Returns title, URL, and snippet candidates; use web_fetch or agent-browser to inspect a result.",
    .param_schema =
        "{\"type\":\"object\","
        "\"properties\":{"
        "\"query\":{\"type\":\"string\",\"description\":\"search query\"},"
        "\"max_results\":{\"type\":\"integer\",\"description\":\"optional result count, default 5, max 10\"}"
        "},\"required\":[\"query\"]}",
    .exec = tool_web_search,
    .read_only = true,
};
