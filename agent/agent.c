/*
 * agent.c — orchestration between user input, LLM turns, and tool execution.
 *
 * The skeleton below is sized for Phase A: one request in, one request out,
 * no persistent state to speak of. Phase B and Phase C will both require
 * you to revisit `struct Agent`, agent_create, and agent_free — treat what
 * is here as a starting point, not a contract.
 */
#include "agent.h"

#include "config.h"
#include "llm_client.h"
#include "message.h"
#include "tools/tools.h"
#include "util.h"
#include "ui/ui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_TURNS 20

static const char AGENT_SYSTEM_TEMPLATE[] =
    "You are a coding agent running in the CLI at %s.\n"
    "Use the provided tools when you need to run shell commands.\n"
    "Return a short, final text reply when the task is done.";

struct Agent {
  char *system_prompt;
  char *last_reply;
  MessageList history;
};

Agent *agent_create(void) {
  Agent *a = calloc(1, sizeof(*a));
  if (!a)
    return NULL;
  a->system_prompt = xasprintf(AGENT_SYSTEM_TEMPLATE, g_config.workdir);
  msg_list_init(&a->history);
  return a;
}

void agent_free(Agent *a) {
  if (!a)
    return;
  free(a->system_prompt);
  free(a->last_reply);
  msg_list_free(&a->history);
  free(a);
}

const char *agent_chat(Agent *a, const char *user_input) {
  /*
   * TODO(student, Part 1A):
   *
   * Drive one user turn:
   *
   *   1. Build a MessageList and push the user message.
   *   2. Call llm_chat. On failure, print the error to stderr and
   *      return NULL.
   *   3. If the response carries no tool calls, cache its content on
   *      `a` (so the returned pointer stays valid until the next call),
   *      release everything else, and return it.
   *   4. Otherwise, push the assistant message into history, execute
   *      the tool the LLM asked for, push the result back as a tool
   *      message (msg_tool_json in message.h), and call llm_chat again.
   */
  

  // MessageList history;
  // msg_list_init(&history);

  char *user_json = msg_user_json(user_input);
  msg_list_push(&a->history,user_json);
  
  // free(user_json);

  LLMResponse resp;
  memset(&resp,0,sizeof(resp));
  char err[256];
  ui_begin_thinking();
  if(llm_chat(&a->history, a->system_prompt, g_config.model, &resp, err, sizeof(err))<0){
    fprintf(stderr,"LLM error: %s\n", err);
    msg_list_free(&a->history);
    return NULL;
  }

  int turns = 0;
  while(resp.n_tool_calls>0){
    turns++;
    if(turns>MAX_TURNS){
      a->last_reply = xstrdup("max turns exceeded");
      free(resp.raw_message);
      free(resp.content);
      for(int i=0;i<resp.n_tool_calls;i++){
        free(resp.tool_calls[i].id);
        free(resp.tool_calls[i].name);
        cJSON_Delete(resp.tool_calls[i].args);
      }
      free(resp.tool_calls);
      msg_list_free(&a->history);
      return a->last_reply;
    }

    msg_list_push(&a->history, resp.raw_message);
    resp.raw_message = NULL;
    //执行tools
    ToolCallView views[resp.n_tool_calls];
    for(int i=0;i<resp.n_tool_calls;i++){
      views[i].name = resp.tool_calls[i].name;
      views[i].args_display = NULL;
    }
    ui_begin_tools(resp.n_tool_calls,views);

    for(int i=0;i<resp.n_tool_calls;i++){
      char * tool_msg;
      bool ok;
      
      if(strcmp(resp.tool_calls[i].name,BASH_TOOL_NAME)!=0){
        ToolResult err_res;
        err_res.ok = false;
        err_res.output = xasprintf("unknown tool: %s", resp.tool_calls[i].name);
        tool_msg = msg_tool_json(resp.tool_calls[i].id, err_res.output);
        free(err_res.output);
        ok = false;
      }else{
        ToolResult tr = bash_tool_exec(resp.tool_calls[i].args);
        tool_msg = msg_tool_json(resp.tool_calls[i].id, tr.output ? tr.output : "");
        ok = tr.ok;
        ui_tool_done(i,ok,tr.output);
        tool_result_free(&tr); 
      }
      msg_list_push(&a->history, tool_msg);
      // free(tool_msg);
    }
    free(resp.content);
    free(resp.raw_message);                                                                                                                                                        
    for (int i = 0; i < resp.n_tool_calls; i++) {
        free(resp.tool_calls[i].id);
        free(resp.tool_calls[i].name);
        cJSON_Delete(resp.tool_calls[i].args);                                                                                                                                     
    }
    free(resp.tool_calls);                                                                                                                                                         
    memset(&resp, 0, sizeof(resp));

    ui_begin_thinking();
    if(llm_chat(&a->history, a->system_prompt, g_config.model,
                   &resp, err, sizeof(err)) < 0) {                                                                                                                                   
      fprintf(stderr, "llm_chat failed: %s\n", err);
      msg_list_free(&a->history);                                                                                                                                                   
      return NULL;                                                                                                                                                               
    }

  }
  free(a->last_reply);
  a->last_reply = resp.content ? resp.content : xstrdup("");
  if (resp.raw_message)
    msg_list_push(&a->history, resp.raw_message);
  free(resp.tool_calls);

  return a->last_reply;
}

MessageList *agent_get_history(Agent *a) {
  return &a->history;
}

void agent_clear_history(Agent *a) {
  msg_list_free(&a->history);
  msg_list_init(&a->history);
  free(a->last_reply);
  a->last_reply = NULL;
}