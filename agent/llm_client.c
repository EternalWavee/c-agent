/*
 * llm_client.c — HTTP+JSON glue between the Agent and the LLM service.
 *
 * Your job: implement llm_chat. Everything else in this file is yours to
 * design. You will certainly want helpers (request construction, response
 * parsing, …); whether and how you decompose them is a decision for you.
 */
#include "llm_client.h"

#include "core/config.h"
#include "core/http.h"
#include "tools/tools.h"
#include "core/util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define LLM_TIMEOUT_SEC 120

int llm_chat(const MessageList *messages, const char *system_prompt,
             const char *model, LLMResponse *out, char *err, size_t err_cap) {
  // (void)messages;
  // (void)system_prompt;
  // (void)model;
  // (void)out;

  /*
   * TODO(student, Part 1A):
   *
   * 1. Build the request body. It is a JSON object with fields
   *    `model`, `messages` (system prompt prepended to the given list),
   *    `tools` (one entry describing the bash tool — see BASH_TOOL_NAME /
   *    BASH_TOOL_DESC / BASH_TOOL_SCHEMA in tools/tools.h), and
   *    `max_tokens` (g_config.max_tokens). Use cJSON (see libs/cJSON.h);
   *    hand-splicing strings will not scale.
   *
   * 2. Open a TCP connection (see http.h) and send a POST to
   *    /api/v1/chat/completions with Authorization: Bearer <api_key>.
   *
   * 3. Read the full response with recv_all, parse it with
   *    http_parse_response, and reject any non-200 status.
   *
   * 4. Parse the JSON body. The assistant message lives at
   *    choices[0].message. Extract `content` (may be missing or empty)
   *    and every entry of `tool_calls` (each has `id`, `function.name`,
   *    `function.arguments`). `arguments` arrives as a JSON *string* on
   *    the wire — cJSON_Parse it back into an object. If the string is
   *    empty, treat it as an empty object.
   *
   * 5. Also keep a serialized copy of the whole assistant message
   *    (cJSON_PrintUnformatted is convenient) in out->raw_message — the
   *    agent will push it into history verbatim so the LLM sees its own
   *    previous reply on the next call.
   *
   * Everything you malloc / cJSON_Parse here has to be freed somewhere.
   * Decide where. `make asan` will tell you if you got it wrong.
   */
  /*目标
  {
    "model": "gpt-4",
    "messages": [
      {"role": "system", "content": "你是一个助手"},
      {"role": "user", "content": "你好"}
    ],
    "tools": [
      {
        "type": "function",
        "function": {
          "name": "bash",
          "description": "Run a shell command...",
          "parameters": {"type":"object",...}
        }
      }
    ],
    "max_tokens": 1024
  }
  
  */
  
  cJSON *req = cJSON_CreateObject();
  cJSON_AddStringToObject(req, "model", model);

  cJSON *messages_array = cJSON_CreateArray();
  cJSON *system_message = cJSON_CreateObject();
  cJSON_AddStringToObject(system_message, "role", "system");
  cJSON_AddStringToObject(system_message, "content", system_prompt);

  cJSON_AddItemToArray(messages_array, system_message);
  //历史消息也是
  for(int i=0;i<messages->len;i++){
    cJSON *msg = cJSON_Parse(messages->items[i]);
    if(msg){
      cJSON_AddItemToArray(messages_array, msg);
    }
  }
  
  cJSON *tool_array = cJSON_CreateArray();
  

  int count;
  ToolDef *const *tools = tool_list(&count);
  for (int i=0;i<count;i++){
  cJSON *tool = cJSON_CreateObject();
  cJSON_AddStringToObject(tool,"type","function");

  cJSON *function = cJSON_CreateObject();
  cJSON_AddStringToObject(function,"name",tools[i]->name);
  cJSON_AddStringToObject(function,"description",tools[i]->desc);
  cJSON *params = cJSON_Parse(tools[i]->param_schema);
  cJSON_AddItemToObject(function, "parameters", params);

  cJSON_AddItemToObject(tool, "function", function);
  cJSON_AddItemToArray(tool_array, tool);
  }

  cJSON_AddItemToObject(req, "messages", messages_array);
  cJSON_AddItemToObject(req, "tools", tool_array);
  cJSON_AddNumberToObject(req, "max_tokens", g_config.max_tokens);

  char *body = cJSON_PrintUnformatted(req);
  size_t body_len = strlen(body);
  cJSON_Delete(req);

  char *header = xasprintf(
      "POST /api/v1/chat/completions HTTP/1.1\r\n"
      "Host: %s:%d\r\n"
      "Authorization: Bearer %s\r\n"
      "Content-Type: application/json\r\n"
      "Content-Length: %zu\r\n"
      "Connection: close\r\n"
      "\r\n",
      g_config.llm_host, g_config.llm_port,
      g_config.api_key,
      body_len
  );


  int fd = tcp_connect(g_config.llm_host, g_config.llm_port, err,err_cap);
  if(fd<0){
    snprintf(err, err_cap, "Failed to connect to LLM server");
    free(header);
    free(body);
    return -1;
  }
  if(send_all(fd,header,strlen(header))<0){
    snprintf(err, err_cap, "Failed to send request header");
    free(header);
    free(body);
    close(fd);
    return -1;
  }

  // fprintf(stderr, "DEBUG: body_len=%zu body=%s\n", body_len, body); 
  if(send_all(fd,body,body_len)<0){
    snprintf(err, err_cap, "Failed to send request body");
    free(header);
    free(body);
    close(fd);
    return -1;
  }
  // fprintf(stderr, "DEBUG: body sent, waiting for response...\n"); 
  char *response = NULL;
  size_t resp_len = 0;
  if(recv_all(fd,LLM_TIMEOUT_SEC,&response,&resp_len,err,err_cap)<0){
    free(header);
    free(body);
    close(fd);
    return -1;
  }
  // fprintf(stderr, "DEBUG: response=%.500s\n", response);
  close(fd);
  free(header);
  free(body);
  
  int status;
  const char *body_start;
  if(http_parse_response(response,&status,&body_start)<0){
    free(response);
    return -1;
  }
  if(status!=200){
    snprintf(err, err_cap, "LLM server returned status %d", status);
    free(response);
    return -1;
  }
  
  cJSON *root = cJSON_Parse(body_start);
  if(!root){
    snprintf(err, err_cap, "Failed to parse JSON response");
    free(response);
    return -1;
  }

  cJSON *choices = cJSON_GetObjectItem(root, "choices");
  cJSON *choice0 = cJSON_GetArrayItem(choices, 0);
  cJSON *message = cJSON_GetObjectItem(choice0, "message");

  
  if(!choices || !choice0 || !message){
    snprintf(err, err_cap, "Malformed LLM response");
    cJSON_Delete(root);
    free(response);
    return -1;
  }

  //content
  cJSON *content_val = cJSON_GetObjectItem(message, "content");
  const char *cv = content_val ? cJSON_GetStringValue(content_val) : NULL;
  out->content = cv ? xstrdup(cv) : NULL;

  //raw_message
  char * raw = cJSON_PrintUnformatted(message);
  out->raw_message = raw;

  //tools
  cJSON *tool_calls = cJSON_GetObjectItem(message, "tool_calls");
  out->n_tool_calls = tool_calls ? cJSON_GetArraySize(tool_calls) : 0;

  
  out->tool_calls = NULL;
  if(out->n_tool_calls>0){
    
    out->tool_calls = malloc(out->n_tool_calls*sizeof(LLMToolCall));
    if(!out->tool_calls){
      snprintf(err, err_cap, "Failed to allocate tool_calls");
      cJSON_Delete(root);
      free(response);
      return -1;
    }
    for(int i=0;i<out->n_tool_calls;i++){
      cJSON *tc = cJSON_GetArrayItem(tool_calls, i);
      cJSON *fn = cJSON_GetObjectItem(tc, "function");
      out->tool_calls[i].id = xstrdup(cJSON_GetStringValue(cJSON_GetObjectItem(tc, "id")));
      out->tool_calls[i].name = xstrdup(cJSON_GetStringValue(cJSON_GetObjectItem(fn, "name")));
      
      cJSON *args_str = cJSON_GetObjectItem(fn, "arguments");
      const char *args_raw = cJSON_GetStringValue(args_str);
      if(args_raw && strlen(args_raw)>0){
        cJSON *args_json = cJSON_Parse(args_raw);
        out->tool_calls[i].args = args_json;
      }else{
        out->tool_calls[i].args = cJSON_CreateObject();
      }
    }
  }

  cJSON_Delete(root);
  free(response);

  return 0;
}

void llm_response_free(LLMResponse *r) {
  if (!r)
    return;
  free(r->content);
  free(r->raw_message);
  for (int i = 0; i < r->n_tool_calls; i++) {
    free(r->tool_calls[i].id);
    free(r->tool_calls[i].name);
    cJSON_Delete(r->tool_calls[i].args);
  }
  free(r->tool_calls);
  memset(r, 0, sizeof(*r));
}

int fetch_models(char *out, size_t out_cap, char *err, size_t err_cap) {
  char *header = xasprintf(
      "GET /api/v1/models HTTP/1.1\r\n"
      "Host: %s:%d\r\n"
      "Authorization: Bearer %s\r\n"
      "Connection: close\r\n"
      "\r\n",
      g_config.llm_host, g_config.llm_port, g_config.api_key);

  int fd = tcp_connect(g_config.llm_host, g_config.llm_port, err, err_cap);
  if (fd < 0) {
    free(header);
    return -1;
  }
  if (send_all(fd, header, strlen(header)) < 0) {
    snprintf(err, err_cap, "Failed to send request");
    free(header);
    close(fd);
    return -1;
  }
  free(header);

  char *response = NULL;
  size_t resp_len = 0;
  if (recv_all(fd, 10, &response, &resp_len, err, err_cap) < 0) {
    close(fd);
    return -1;
  }
  close(fd);

  int status;
  const char *body;
  if (http_parse_response(response, &status, &body) < 0) {
    snprintf(err, err_cap, "Failed to parse HTTP response");
    free(response);
    return -1;
  }
  if (status != 200) {
    snprintf(err, err_cap, "Server returned status %d", status);
    free(response);
    return -1;
  }

  cJSON *root = cJSON_Parse(body);
  if (!root) {
    snprintf(err, err_cap, "Failed to parse JSON");
    free(response);
    return -1;
  }

  cJSON *data = cJSON_GetObjectItem(root, "data");
  if (!data || !cJSON_IsArray(data)) {
    snprintf(err, err_cap, "Malformed response: missing 'data' array");
    cJSON_Delete(root);
    free(response);
    return -1;
  }

  out[0] = '\0';
  size_t pos = 0;
  int count = cJSON_GetArraySize(data);
  for (int i = 0; i < count; i++) {
    cJSON *item = cJSON_GetArrayItem(data, i);
    cJSON *id = cJSON_GetObjectItem(item, "id");
    const char *name = id ? cJSON_GetStringValue(id) : NULL;
    if (!name)
      continue;
    size_t len = strlen(name);
    if (pos + len + 2 >= out_cap)
      break;
    if (pos > 0)
      out[pos++] = '\n';
    memcpy(out + pos, name, len);
    pos += len;
  }
  out[pos] = '\0';

  cJSON_Delete(root);
  free(response);
  return 0;
}