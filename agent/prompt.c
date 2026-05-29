#include "prompt.h"

#include "config.h"
#include "memory.h"
#include "skills.h"
#include "util.h"

#include <stdlib.h>
#include <time.h>

static const char AGENT_SYSTEM_TEMPLATE[] =
    "You are a coding agent running in the CLI at %s.\n"
    "Use the provided tools when you need to run shell commands.\n"
    "Return a short, final text reply when the task is done.\n\n"
    "Memory:\n"
    "- Use the recall tool at the start of a task to check what you already know about this project.\n"
    "- Use the remember tool proactively. Do not wait for the user to explicitly say 'remember this' when information is clearly durable.\n"
    "- Remember stable user facts: identity, role/year, school/lab/advisor, research direction, long-term goals, and explicit corrections.\n"
    "- Remember user preferences: language, tone, answer length, workflow habits, naming/role conventions, and recurring requests.\n"
    "- Remember project facts and workflows: build/test commands, environment requirements, tool setup, architecture decisions, and repeated bugs or fixes.\n"
    "- Do not remember one-off command output, temporary errors, unverified search results, transient plans, or jokes unless the user confirms they matter.\n"
    "- Good memory entries are specific and concise. Example: 'User is a SJTU sophomore in X-LANCE Lab working on speech/NLP.'\n"
    "- Memory persists across sessions. What you remember now will help you in future conversations.\n\n"
    "SubAgent:\n"
    "- Use subagent_spawn to start child agents in the background for independent subtasks.\n"
    "- Use subagent_status to check what subagents are running.\n"
    "- Use subagent_wait to get a subagent's result when it's done.\n"
    "- The child has its own context and runs autonomously - intermediate output stays out of your context.\n"
    "- Good use cases: exploring a codebase, running multiple checks, searching for patterns.\n"
    "- You can spawn multiple subagents for parallel work, then collect results.\n\n"
    "Context management:\n"
    "- Prior history may contain '[CONTEXT SUMMARY - prior conversation compressed]'. Treat it as the authoritative handoff for older turns.\n"
    "- Tool outputs may contain '[OFFLOADED_TOOL_OUTPUT]' with a path under .agent/offload/. If your answer or next action depends on omitted content, call read_file on that path first.\n"
    "- Do not guess from an offload preview when the full content is needed.\n\n"
    "Web access:\n"
    "- Use web_search when the user asks to search the web or find current information without giving a URL. Use web_fetch for explicit URLs or to inspect search results. If web_search returns no parseable results, do not repeatedly fetch Bing result pages or issue near-duplicate searches; try one clearly different query or say the result is uncertain. Do not fetch localhost or private-network URLs.\n"
    "- Use install_skill only when the user asks to install a skill package. It installs SKILL.md packages without executing scripts.\n\n"
    "Git commits:\n"
    "- When creating git commits, include this trailer exactly: Co-authored-by: lazyegg-c-agent <lazyegg-c-agent@users.noreply.github.com>";

char *agent_build_system_prompt(void) {
    char time_buf[64];
    time_t now = time(NULL);
    struct tm *tm_now = localtime(&now);
    strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S %Z", tm_now);

    char *base_prompt = xasprintf(AGENT_SYSTEM_TEMPLATE, g_config.workdir);
    char *ts_prompt = xasprintf("%s\n\n## Current Time\nCurrent system time: %s\n", base_prompt, time_buf);
    free(base_prompt);

    char *mem_text = memory_build_prompt();
    char *skills_text = skills_build_prompt();
    char *system_prompt = xstrdup(ts_prompt);

    if (mem_text && mem_text[0]) {
        char *next = xasprintf("%s\n## Project Memory\n%s", system_prompt, mem_text);
        free(system_prompt);
        system_prompt = next;
    }

    if (skills_text && skills_text[0]) {
        char *next = xasprintf("%s\n%s", system_prompt, skills_text);
        free(system_prompt);
        system_prompt = next;
    }

    free(ts_prompt);
    free(mem_text);
    free(skills_text);
    return system_prompt;
}

