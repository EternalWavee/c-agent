<h1 align="center">C Agent</h1>

<p align="center">
  <img src="https://img.shields.io/badge/CS2313-OS%20Course%20Project-blueviolet?style=flat-square" alt="CS2313"/>
  <img src="https://img.shields.io/badge/language-C11-blue?style=flat-square&logo=c" alt="C11"/>
  <img src="https://img.shields.io/badge/platform-Linux-lightgrey?style=flat-square&logo=linux" alt="Linux"/>
  <img src="https://img.shields.io/badge/build-Make-green?style=flat-square&logo=gnu" alt="Make"/>
</p>

<p align="center">
  A lightweight, terminal-based coding agent written in pure C.<br/>
  <a href="README.md">中文文档</a>
</p>

C Agent is a terminal-based coding assistant powered by LLM tool-calling. Users describe tasks in natural language, and the agent autonomously invokes tools — running shell commands, reading and writing files, editing code — to complete them. Built with a ReAct reasoning loop, parallel read-only execution, automatic context management (offload + summary), and workspace sandbox isolation.

This project is the course design for **CS2313 Operating Systems** at Shanghai Jiao Tong University.

---

## Highlights

- **ReAct loop** — multi-turn reasoning with tool calling until task completion
- **4 built-in tools** — `bash`, `read_file`, `write_file`, `edit_file`
- **Parallel execution** — read-only tools dispatched concurrently via pthreads
- **Context management** — automatic offload (large outputs to disk) and summary (LLM-based compression)
- **Session persistence** — append-only log + checkpoint architecture with crash recovery, auto-naming, and interactive session management
- **Project memory** — LLM-driven persistent knowledge store (`.agent/memory.md`), the agent learns and remembers across sessions
- **Sandbox safety** — all file paths confined to the workspace directory
- **Dangerous command filter** — blocks destructive shell patterns before execution
- **Terminal UI** — real-time spinner and per-tool status display

---

## Quick Start

### Prerequisites

- GCC or Clang with C11 support
- GNU Make
- An LLM endpoint compatible with OpenAI API format

### Build

```bash
make            # development build
make asan       # AddressSanitizer + UBSan
make tsan       # ThreadSanitizer
```

### Configure

```bash
export API_KEY="api-key"
export MODEL_ID="qwen3coder"
export LLM_HOST="127.0.0.1"
export LLM_PORT="18080"
```

### Run

```bash
# With caddy proxy (recommended)
./start.sh

# Or manually
caddy reverse-proxy --from :18080 --to https://models.sjtu.edu.cn --change-host-header &
./build/c-agent
```

```
C Agent - /home/user/project
Model: qwen3coder
Type 'exit' to quit.
> find all .c files and count their total lines
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
| `recall` | Read project memory from previous sessions | Yes |

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

The agent maintains a persistent memory file at `.agent/memory.md` that stores knowledge across sessions.

**How it works:**

1. The LLM uses the `remember` tool to save important findings during conversation
2. The LLM uses the `recall` tool (or `read_file`) to read past knowledge when needed
3. The `/memory` command triggers the LLM to review the conversation and save what it learned
4. The system prompt guides the LLM to actively use memory — it learns what's worth remembering over time

**Memory format** (human-readable markdown):

```markdown
# Project Memory
- **[architecture]** 2026-05-24 14:30: Uses WAL pattern — checkpoint.json + log.jsonl
- **[build]** 2026-05-24 14:31: Run `make` to build, binary at build/c-agent
- **[convention]** 2026-05-24 14:32: File tools must call resolve_workspace_path before opening
```

**Write paths:**
- LLM actively calls `remember` tool (primary)
- `/memory` command triggers LLM summarization
- User manually edits `.agent/memory.md`

**Read paths:**
- LLM calls `recall` tool
- LLM calls `read_file` on `.agent/memory.md`
- `/memory` command (triggers read + summarize + write cycle)

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
│   ├── executor.c          # Parallel / serial dispatch
│   ├── sandbox.c           # Workspace path containment
│   ├── bash.c              # Shell command execution
│   ├── read.c              # File reader
│   ├── write.c             # File writer
│   ├── edit.c              # Substring replacement
│   └── memory_tool.c       # remember + recall tools
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
- [x] Project memory (LLM-driven persistent knowledge store in .agent/memory.md)
- [x] Terminal UI with spinner and per-tool status

### Planned

- [ ] **Evaluation harness** — end-to-end benchmark scenarios with metrics
- [ ] **SubAgent** — spawn child agents for independent subtasks
- [x] **Memory** — project-level knowledge persistence across sessions
- [ ] **Skill system** — on-demand prompt injection for specialized tasks

---

## Limitations & Future Work

**Current limitations:**

- **Limited tool set** — only 4 tools (`bash`, `read_file`, `write_file`, `edit_file`). Missing common capabilities like web search, codebase indexing, diff viewing, and git operations.
- **No streaming** — LLM responses are received in full before display. Long responses feel unresponsive.
- **Single-model support** — no easy way to switch between different LLM providers or mix models for different tasks.
- **Basic context heuristics** — the token estimator is a rough character-count heuristic, not a real tokenizer. Budget thresholds may fire too early or too late.
- **No authentication** — `API_KEY` is stored in plaintext environment variables.

**Potential improvements:**

- **Richer tool ecosystem** — add tools like `grep`, `find`, `git`, `web_fetch`, `code_search` to handle more complex workflows
- **Streaming output** — display LLM responses token-by-token for better perceived latency
- **Plugin architecture** — allow users to register custom tools at runtime via shared libraries or config files
- **Smarter context policies** — use actual tokenizers (tiktoken) and implement priority-based eviction (e.g. keep high-value tool results, drop low-info chat)
- **Multi-agent collaboration** — let the main agent delegate subtasks to specialized child agents with isolated contexts
- **Persistent memory** — store project-level knowledge (codebase structure, conventions, past decisions) that survives across sessions

---

## Try It Out

```bash
mkdir -p /tmp/agent-scratch && cd /tmp/agent-scratch
export API_KEY="your-key"
/path/to/build/c-agent
> write a C program that prints the first 20 primes to primes.c, then compile and run it
```

The agent will automatically write the file, compile it, run it, and report the output.
