/*
 * bash tool — run a shell command in a forked child and return its output
 * as a ToolResult.
 *
 * The child side (pipe setup, fork, dup2, execl) is given below — you have
 * already written this pattern in Project 1. What the LLM actually needs is
 * the *other half*: after fork, the parent holds pipefd[0] and pid, and
 * must turn that into a ToolResult the agent can send back.
 */
#include "tools.h"
#include "core/util.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

const char *BASH_TOOL_NAME = "bash";
const char *BASH_TOOL_DESC =
    "Run a shell command and return its combined stdout/stderr.";
const char *BASH_TOOL_SCHEMA =
    "{\"type\":\"object\","
    "\"properties\":{\"command\":{\"type\":\"string\","
    "\"description\":\"The shell command to execute\"}},"
    "\"required\":[\"command\"]}";

ToolResult bash_tool_exec(cJSON *args) {
  const char *cmd = cJSON_GetStringValue(cJSON_GetObjectItem(args, "command"));
  if (!cmd)
    return (ToolResult){.ok = false,
                        .output = xstrdup("missing 'command' argument")};

  int pipefd[2];
  if (pipe(pipefd) != 0)
    return (ToolResult){
        .ok = false,
        .output = xasprintf("pipe failed: %s", strerror(errno)),
    };

  pid_t pid = fork();
  if (pid < 0) {
    int e = errno;
    close(pipefd[0]);
    close(pipefd[1]);
    return (ToolResult){.ok = false,
                        .output = xasprintf("fork: %s", strerror(e))};
  }

  if (pid == 0) {
    close(pipefd[0]);
    /* dprintf + _exit avoid stdio-buffer double-flush after fork. */
    if (dup2(pipefd[1], STDOUT_FILENO) < 0 ||
        dup2(pipefd[1], STDERR_FILENO) < 0)
      _exit(127);
    close(pipefd[1]);
    execl("/bin/sh", "sh", "-c", cmd, (char *)NULL);
    dprintf(STDERR_FILENO, "exec failed: %s\n", strerror(errno));
    _exit(127);
  }

  close(pipefd[1]);

  /*
   * TODO(student, Part 1A):
   *
   * You are in the parent. The child is running `cmd` with its stdout and
   * stderr both wired to pipefd[1]; you hold pipefd[0] and pid.
   *
   * Produce a ToolResult describing what the child did. The LLM will read
   * whatever you put in .output verbatim on the next turn, so think about
   * what it would actually want to see — *including* when the command
   * failed. A non-zero exit or a fatal signal is information the LLM needs
   * to react to; see waitpid(2) and the WIFEXITED / WEXITSTATUS /
   * WIFSIGNALED / WTERMSIG macros in <sys/wait.h>.
   *
   * Remember to close pipefd[0] and reap the child before returning.
   */
  char buffer[4096];
  char *output = NULL;
  ssize_t n;
  while ((n = read(pipefd[0], buffer, sizeof(buffer)-1)) > 0){
    buffer[n] = '\0';
    char *old = output;
    output = xasprintf("%s%s", output ? output : "", buffer);
    free(old);
  }
  close(pipefd[0]);

  int status;
  waitpid(pid, &status, 0);
  bool ok = true;
  char *info = NULL;

  if(WIFEXITED(status)){
    int ec = WEXITSTATUS(status);
    if(ec != 0){
      ok = false;
      info = xasprintf("command exited with code %d", ec);
    }
  }
  
  if(WIFSIGNALED(status)){
      int sig = WTERMSIG(status);
      ok = false;
      info = xasprintf("command killed by signal %d", sig);
  }

  char *final;
  if(output&&info){
    final = xasprintf("%s\n%s", output, info);
  }
  else if(info){
    final = info;
    info = NULL;
  }
  else{
    final = output?output:xstrdup("");
    output = NULL;
  }
  free(output);
  free(info);

  return (ToolResult){.ok = ok,
                      .output = final};
  }

/* Dangerous command patterns — checked before execution. */
static const char *DANGEROUS_PATTERNS[] = {
    "rm -rf /",
    "rm -rf /*",
    "rm -rf ~",
    "rm -rf .",
    ":(){ :|:& };:",   /* fork bomb */
    "mkfs.",
    "dd if=/dev/zero",
    "dd if=/dev/random",
    "> /dev/sda",
    "curl|sh",
    "curl|bash",
    "wget|sh",
    "wget|bash",
    "curl |sh",
    "curl |bash",
    "wget |sh",
    "wget |bash",
    "chmod -R 777 /",
    "chmod 777 /",
    "mv / /",
    NULL,
};

static bool is_dangerous(const char *cmd) {
  for (int i = 0; DANGEROUS_PATTERNS[i]; i++) {
    if (strstr(cmd, DANGEROUS_PATTERNS[i]))
      return true;
  }
  return false;
}

ToolResult tool_bash(cJSON *args) {
  const char *cmd = cJSON_GetStringValue(cJSON_GetObjectItem(args, "command"));
  if (!cmd)
    return (ToolResult){.ok = false,
                        .output = xstrdup("missing 'command' argument")};

  if (is_dangerous(cmd))
    return (ToolResult){
        .ok = false,
        .output = xasprintf("BLOCKED: command '%s' is too dangerous to run", cmd),
    };

  return bash_tool_exec(args);
}

// register for bash tool
ToolDef bash_def = {
    .name = "bash",
    .desc = "Run a shell command and return its combined stdout/stderr.",
    .param_schema = "{\"type\":\"object\","
                    "\"properties\":{\"command\":{\"type\":\"string\","
                    "\"description\":\"The shell command to execute\"}},"
                    "\"required\":[\"command\"]}",
    .exec = tool_bash,
    .read_only = false,
};