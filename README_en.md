<h1 align="center">C Agent</h1>

<p align="center">
  <img src="https://img.shields.io/badge/CS2313-OS%20Course%20Project-blueviolet?style=flat-square" alt="CS2313"/>
  <img src="https://img.shields.io/badge/language-C11-blue?style=flat-square&logo=c" alt="C11"/>
  <img src="https://img.shields.io/badge/platform-Linux-lightgrey?style=flat-square&logo=linux" alt="Linux"/>
  <img src="https://img.shields.io/badge/build-Make-green?style=flat-square&logo=gnu" alt="Make"/>
</p>

<p align="center">
  A lightweight, terminal-based coding agent written in pure C.<br/>
  <a href="README_zh.md">中文文档</a>
</p>

C Agent is a terminal-based coding assistant powered by LLM tool-calling. Users describe tasks in natural language, and the agent autonomously invokes tools — running shell commands, reading and writing files, editing code — to complete them. Built with a ReAct reasoning loop, parallel read-only execution, automatic context management (offload + summary), and workspace sandbox isolation.

This project is the course design for **CS2313 Operating Systems** at Shanghai Jiao Tong University.

---

## Highlights

- **ReAct loop** — multi-turn reasoning with tool calling until task completion
- **11 built-in tools** — `bash`, `read_file`, `write_file`, `edit_file`, `remember`, `recall`, `memory_delete`, `memory_update`, `subagent_spawn`, `subagent_status`, `subagent_wait`
- **Parallel execution** — read-only tools dispatched concurrently via pthreads
- **Context management** — automatic offload (large outputs to disk) and summary (LLM-based compression)
- **Session persistence** — append-only log + checkpoint architecture with crash recovery, auto-naming, and interactive session management
- **Project memory** — LLM-driven persistent knowledge store (`.agent/memory.md`), the agent learns and remembers across sessions
- **Sandbox safety** — all file paths confined to the workspace directory
- **Dangerous command filter** — blocks destructive shell patterns before execution
- **Terminal UI** — real-time spinner and per-tool status display

---

## Quick Start

```bash
export API_KEY="sjtu-api-key"
./start.sh
```

---

## Environment Variables

| Variable | Default | Description |
|----------|---------|-------------|
| `API_KEY` | `none` | Bearer token for LLM authentication |
| `MODEL_ID` | `deepseek-chat` | Model identifier |
| `LLM_HOST` | `127.0.0.1` | LLM server host |
| `LLM_PORT` | `18080` | LLM server port |
| `MAX_TOKENS` | `8000` | Max tokens per LLM response |
| `CONTEXT_WINDOW` | `32000` | Total token budget for conversation history |
| `OFFLOAD_THRESHOLD` | `0.8` | Fraction of window that triggers output offload |
| `SUMMARY_THRESHOLD` | `0.8` | Fraction of window that triggers history summarization |

---

## REPL Commands

| Command | Description |
|---------|-------------|
| `/model` | Interactive model picker |
| `/session` | Browse and restore saved sessions (interactive selector) |
| `/session new` | Create a new session |
| `/session name X` | Rename current session |
| `/session delete X` | Delete a session |
| `/session restore X` | Restore a session by id |
| `/memory` | Trigger LLM to summarize and save what it learned |
| `/help` | Show available commands |
| `exit` / `quit` / `q` | Exit |

---

## Built-in Tools

| Tool | Description | Read-only |
|------|-------------|-----------|
| `bash` | Run a shell command | No |
| `read_file` | Read file contents | Yes |
| `write_file` | Write contents to a file | No |
| `edit_file` | Replace first occurrence of text in a file | No |
| `remember` | Store knowledge into persistent project memory | No |
| `recall` | Read project memory (supports keyword/type filtering) | Yes |
| `memory_delete` | Delete a memory entry by index | No |
| `memory_update` | Update a memory entry by index | No |
| `subagent_spawn` | Spawn a background child agent for independent subtasks | No |
| `subagent_status` | List all subagents and their status | Yes |
| `subagent_wait` | Wait for a subagent to finish and get its result | No |

Read-only tools are automatically dispatched in parallel. State-changing tools run serially.

---

## Context Management

When the conversation grows too large, two policies reclaim space automatically:

1. **Offload** — moves large tool outputs to `.agent/offload/`, replacing them with compact placeholders that the LLM can use to recover the data via `read_file`
2. **Summary** — compresses older conversation history into a single summary message using the LLM itself

Both are triggered by token budget thresholds and run before each LLM request.

---

## Session System

Each session is stored as a directory under `.agent/sessions/<id>/`:

```
.agent/sessions/
  20260524_093000/
    meta.json            # name, model, timestamps, message count
    checkpoint.json      # full message snapshot (atomic write)
    log.jsonl            # append-only log since last checkpoint
```

- Messages are appended to `log.jsonl` after each turn
- Every 10 messages, a checkpoint is written and the log is truncated
- On startup, a fresh session is created by default; use `/session` to restore previous ones
- Sessions are auto-named with the last user message
- Crash-safe: partial log lines are skipped on recovery

---

## Memory System

Inspired by the agentmemory architecture, the agent maintains a persistent memory file at `.agent/memory.md` with a short-term → long-term consolidation pipeline.

**How it works:**

```
During session: tool calls → auto-capture into short-term buffer (in-memory)
On shutdown:    short-term → LLM#1 extract → LLM#2 merge with existing → memory.md
On startup:     memory.md → injected into system prompt → agent "remembers"
```

**Memory types** (6 categories):

| Type | Meaning | Example |
|------|---------|---------|
| `pattern` | Code patterns / conventions | "Project uses snake_case for functions" |
| `preference` | User preferences | "User prefers concise comments" |
| `architecture` | Architecture knowledge | "Agent connects to LLM via TCP" |
| `bug` | Known issues | "session_load doesn't restore model" |
| `workflow` | Build / test workflows | "Build with make, test with make test" |
| `fact` | General facts | "Project is CS2313 course design" |

**Two-step consolidation** (runs on session shutdown):

1. **LLM#1 Extract** — compress raw tool observations into structured knowledge entries
2. **LLM#2 Merge** — merge new entries with existing memory.md, deduplicate, organize by category

**Write paths:**
- Auto-capture from tool calls (`write_file`, `edit_file`, `bash`, `read_file`)
- LLM actively calls `remember` tool
- User manually edits `.agent/memory.md`

**Read paths:**
- System prompt injection (automatic on startup)
- LLM calls `recall` tool (supports `keyword` and `type` filters)

**Modify / Delete:**
- `recall` returns entries with `[index]` prefixes
- LLM calls `memory_delete <index>` to remove an entry
- LLM calls `memory_update <index> <content>` to replace an entry

---

## Testing

```bash
make test               # all integration tests
make test-cunit         # C unit tests (registry, sandbox, offload, summary)
make test-asan          # full suite under AddressSanitizer
make test-tsan          # concurrency tests under ThreadSanitizer
```

---

## Project Structure

```
.
├── main.c                  # Entry point and REPL
├── session.c               # Session persistence (log + checkpoint)
├── memory.c                # Project memory (.agent/memory.md)
├── cmd.c                   # Slash command dispatch
├── config.c                # Environment-based configuration
├── agent/
│   ├── agent.c             # Core conversation loop (ReAct)
│   └── llm_client.c        # HTTP transport + JSON wire format
├── context/
│   ├── context.c           # Context budget and policy engine
│   ├── policy_offload.c    # Lossless offload to disk
│   └── policy_summary.c    # Lossy compression via LLM
├── tools/
│   ├── registry.c          # Static tool registry
│   ├── init.c              # Tool registration initialization
│   ├── executor.c          # Parallel / serial dispatch
│   ├── sandbox.c           # Workspace path containment
│   ├── bash.c              # Shell command execution
│   ├── read.c              # File reader
│   ├── write.c             # File writer
│   ├── edit.c              # Substring replacement
│   ├── memory_tool.c       # remember + recall + delete + update tools
│   └── subagent.c          # Background child agents (spawn/status/wait)
├── ui/
│   ├── ui.c                # Event dispatch
│   └── render.c            # Terminal rendering thread
├── libs/
│   └── cJSON/              # JSON parser
├── start.sh                # One-liner: caddy proxy + agent
└── Makefile
```

---

## Roadmap

### Done

- [x] Single-turn and multi-turn tool calling (ReAct loop)
- [x] Tool registry with static registration
- [x] Parallel execution of read-only tools
- [x] Workspace sandbox (path escape prevention)
- [x] Dangerous command filtering
- [x] Context offload (large outputs to disk)
- [x] Context summary (LLM-based history compression)
- [x] Session persistence (append-only log + checkpoint, crash recovery, auto-naming)
- [x] Project memory (LLM-driven persistent knowledge store in .agent/memory.md) with filtering, delete, and update
- [x] Terminal UI with spinner and per-tool status
- [x] SubAgent (background child agents for parallel independent subtasks)

### Planned

- [ ] **Evaluation harness** — end-to-end benchmark scenarios with metrics
- [x] **SubAgent** — spawn child agents for independent subtasks
- [x] **Memory** — project-level knowledge persistence across sessions, with filtering, delete, and update
- [x] **SubAgent** — background child agents for parallel independent subtasks
- [ ] **Skill system** — on-demand prompt injection for specialized tasks

---

## Limitations & Future Work

**Current limitations:**

- **Limited tool set** — 4 base tools (`bash`, `read_file`, `write_file`, `edit_file`) plus 7 extended tools (memory management, sub-agents). Still missing common capabilities like web search, codebase indexing, diff viewing, and git operations.
- **No streaming** — LLM responses are received in full before display. Long responses feel unresponsive.
- **Single-model support** — no easy way to switch between different LLM providers or mix models for different tasks.
- **Basic context heuristics** — the token estimator is a rough character-count heuristic, not a real tokenizer. Budget thresholds may fire too early or too late.
- **No authentication** — `API_KEY` is stored in plaintext environment variables.

**Potential improvements:**

- **Richer tool ecosystem** — add tools like `grep`, `find`, `git`, `web_fetch`, `code_search` to handle more complex workflows
- **Streaming output** — display LLM responses token-by-token for better perceived latency
- **Plugin architecture** — allow users to register custom tools at runtime via shared libraries or config files
- **Smarter context policies** — use actual tokenizers (tiktoken) and implement priority-based eviction (e.g. keep high-value tool results, drop low-info chat)
- **Multi-agent collaboration** — basic implementation done (`subagent_spawn`/`subagent_status`/`subagent_wait`), supports background parallel subtasks
- **Persistent memory** — implemented (`remember`/`recall`/`memory_delete`/`memory_update`), supports filtering, deletion, and update

---

## Try It Out

```bash
mkdir -p /tmp/agent-scratch && cd /tmp/agent-scratch
export API_KEY="your-key"
/path/to/build/c-agent
> write a C program that prints the first 20 primes to primes.c, then compile and run it
```

The agent will automatically write the file, compile it, run it, and report the output.
