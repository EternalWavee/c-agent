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
- **16 built-in tools** — `bash`, `read_file`, `write_file`, `edit_file`, `remember`, `recall`, `memory_delete`, `memory_update`, `subagent_spawn`, `subagent_status`, `subagent_wait`, `current_time`, `load_skill`, `web_fetch`, `web_search`, `install_skill`
- **Parallel execution** — read-only tools dispatched concurrently via pthreads
- **Context management** — automatic offload (session-scoped files + manifest) and structured summary handoff
- **Session persistence** — append-only log + checkpoint architecture with crash recovery, auto-naming, and interactive session management
- **Project memory** — `.agent/memory/` index plus six typed files for persistent project knowledge
- **Sandbox safety** — all file paths confined to the workspace directory
- **Dangerous command filter** — blocks destructive shell patterns before execution
- **Terminal UI** — real-time spinner and per-tool status display

---

## Quick Start

Requirements: C compiler, `make`, POSIX shell, and `caddy`. `start.sh` starts a local proxy to the SJTU model service.

### Linux

```bash
sudo apt install -y build-essential make caddy
export API_KEY="sjtu-api-key"
./start.sh
```

### macOS

```bash
xcode-select --install
brew install caddy
export API_KEY="sjtu-api-key"
./start.sh
```

### Windows

Native Windows builds are not supported. Use WSL2:

```powershell
wsl --install
```

Then follow the Linux steps inside WSL.

Manual startup:

```bash
make
caddy reverse-proxy --from :18080 --to https://models.sjtu.edu.cn --change-host-header
# In another terminal
./build/c-agent
```

---

## Milestone

> 🚀 **2026.05.27** — c-agent takes shape with full tool-calling, memory management, and autonomous coding capabilities. Built on our C framework, powered by DeepSeek provided by Shanghai Jiao Tong University, [this iteration](https://github.com/EternalWavee/c-agent/commit/2b2475aadaeb43fde3a1b946ea977ec38496a56c) was completed entirely through natural language instructions.

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
| `/memory` | Ask the agent to call `remember` for important knowledge learned in the current conversation |
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
| `current_time` | Get the current system time | Yes |
| `load_skill` | Load full project/user `SKILL.md` instructions | Yes |
| `web_fetch` | Fetch external `http/https` URLs; blocks localhost/private networks | Yes |
| `web_search` | Search Bing RSS results and return title, URL, and snippet candidates | Yes |
| `install_skill` | Install a skill package from local dir, git repo, or GitHub tree URL | No |

Read-only tools are automatically dispatched in parallel. State-changing tools run serially.

---

## Context Management

When the conversation grows too large, two policies reclaim space before each LLM request:

1. **Offload** — moves large tool outputs to `.agent/offload/` and replaces the original tool message with a short preview plus a structured recovery reference. Interactive sessions are isolated by session id: `.agent/offload/<session_id>/<n>.txt`; non-interactive/test runs keep the legacy `.agent/offload/<n>.txt` layout.
2. **Summary** — asks the LLM to compress older conversation history into a structured handoff message while preserving recent raw messages. The handoff keeps sections such as `Goal`, `Completed`, `Current State`, `Key Files`, `Decisions`, `Offloaded References`, `Open Questions`, and `Next Steps`.

An offloaded tool message looks like:

```text
[OFFLOADED_TOOL_OUTPUT]
id: 0
path: .agent/offload/<session_id>/0.txt
size_bytes: ...
manifest: .agent/offload/<session_id>/MANIFEST.md
recovery: call read_file ...
```

Each offload directory also has a `MANIFEST.md` recording the source tool, arguments, size, and preview for every offloaded file. The system prompt tells the agent to call `read_file` before relying on omitted content, instead of guessing from the preview.

Offload runs before summary. If offload reduces usage below the summary threshold, summary is skipped.

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

## Web Access

`web_fetch` provides minimal web access for explicit `http/https` URLs. It follows redirects, enforces timeout/size limits, and blocks localhost/private-network hosts. It is intended for official docs, remote `SKILL.md` files, GitHub raw files, and similar resources.

`web_search` provides simple search by requesting Bing `format=rss` search results and parsing the first items. It does not use the Bing API; if RSS parsing fails, try a different query or use a known URL.

`install_skill` installs skill packages from local directories, git repo URLs, or GitHub tree URLs. It only copies a directory containing `SKILL.md`; it does not execute `scripts/`. Existing skills are not overwritten unless `force=true`.

---

## Skill System

Skills provide task-specific instructions on demand, without stuffing every long prompt into the system prompt. Package layout:

```text
Project: .agent/skills/<skill-name>/
User: ~/.c-agent/skills/<skill-name>/
  SKILL.md          # required skill entrypoint
  scripts/          # optional helper scripts
  references/       # optional reference docs
  assets/           # optional templates/resources
```

On startup, the agent scans `SKILL.md` metadata from project-level `.agent/skills/` and user-level `~/.c-agent/skills/`, then builds a compact skill manifest. Project skills override user skills with the same name. Frontmatter, markdown tables, and simple `key value` metadata are supported. Common fields:

```text
name: benchmark-research-skill
description: Use when analyzing benchmarks, datasets, metrics, baselines, or related work.
allowed-tools: Read, Write, Edit, Bash, WebFetch, Grep, Glob
```

When a skill is relevant, the agent calls `load_skill {"name":"..."}` to read the full `SKILL.md`. Users can also ask the agent to use `install_skill` to install a package from a local directory, git repo, or GitHub tree URL. The default target is project scope `.agent/skills/`; `scope=user` installs to `~/.c-agent/skills/`. `scripts/`, `references/`, and `assets/` are supported as ordinary package directories in the first version; scripts still run through the existing `bash` tool, with no dedicated `run_skill_script` yet.

---

## Memory System

The agent maintains persistent project knowledge under `.agent/memory/`. Storage is split into an index plus six typed files:

```text
.agent/memory/
  MEMORY.md          # index of memory files
  pattern.md         # code patterns / conventions
  preference.md      # user preferences
  architecture.md    # architecture knowledge
  bug.md             # known issues
  workflow.md        # build/test/run workflows
  fact.md            # stable facts
```

**How it works:**

```text
On startup:  preference.md + fact.md → system prompt, so the agent remembers identity, preferences, and stable facts
During work: recall can query the full memory store by keyword/type
On write:    remember writes to the matching typed file and deduplicates exact entries
Maintenance: memory_delete/memory_update operate on global indices returned by recall
```

If a legacy `.agent/memory.md` exists, entries in `- [type] content` format are migrated into the new directory structure on startup. The old two-step LLM consolidation on shutdown is disabled; future memory compaction should be handled by a restricted memory subagent that edits `.agent/memory/` directly.

**Memory types** (6 categories):

| Type | Meaning | Example |
|------|---------|---------|
| `pattern` | Code patterns / conventions | "Project uses snake_case for functions" |
| `preference` | User preferences | "User prefers concise comments" |
| `architecture` | Architecture knowledge | "Agent connects to LLM via TCP" |
| `bug` | Known issues | "session_load doesn't restore model" |
| `workflow` | Build / test workflows | "Build with make, test with make test" |
| `fact` | General facts | "Project is CS2313 course design" |

**Write paths:**
- The agent proactively calls `remember`; it should not wait for the user to explicitly say "remember this"
- It should remember stable user facts, long-term preferences, fixed project workflows, architecture decisions, repeated bugs/fixes, and explicit corrections
- It should not remember one-off command output, temporary errors, unverified search results, transient plans, or jokes unless the user confirms they matter
- `/memory` asks the agent to review the current conversation and call `remember` once per durable item
- Users may manually edit `.agent/memory/*.md`

**Read paths:**
- Startup injects only `preference` and `fact` entries, avoiding full memory bloat in the system prompt
- The agent calls `recall` for the full memory store (supports `keyword` and `type` filters)

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
├── memory.c                # Project memory (.agent/memory/ index + typed files)
├── skills.c                # Skill metadata scanning and SKILL.md loading
├── cmd.c                   # Slash command dispatch
├── config.c                # Environment-based configuration
├── agent/
│   ├── agent.c             # Core conversation loop (ReAct)
│   ├── prompt.c           # System prompt assembly (rules, time, memory injection)
│   └── llm_client.c        # HTTP transport + JSON wire format
├── context/
│   ├── context.c           # Context budget and policy engine
│   ├── policy_offload.c    # Lossless offload to disk (session scoped + manifest)
│   └── policy_summary.c    # Lossy compression via structured LLM handoff
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
│   ├── skill_tool.c        # load_skill tool
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
- [x] Project memory (.agent/memory/ index + six typed files) with filtering, delete, and update
- [x] Terminal UI with spinner and per-tool status
- [x] SubAgent (background child agents for parallel independent subtasks)

### Planned

- [ ] **Evaluation harness** — end-to-end benchmark scenarios with metrics
- [x] **SubAgent** — spawn child agents for independent subtasks
- [x] **Memory** — project-level knowledge persistence across sessions, with filtering, delete, and update
- [x] **SubAgent** — background child agents for parallel independent subtasks
- [x] **Skill system** — scans `.agent/skills/<name>/SKILL.md` metadata and loads full instructions with `load_skill`

---

## Limitations & Future Work

**Current limitations:**

- **Limited tool set** — 4 base tools (`bash`, `read_file`, `write_file`, `edit_file`) plus 8 extended tools (memory management, sub-agents). Still missing common capabilities like web search, codebase indexing, diff viewing, and git operations.
- **No streaming** — LLM responses are received in full before display. Long responses feel unresponsive.
- **Single-model support** — no easy way to switch between different LLM providers or mix models for different tasks.
- **Basic context heuristics** — the token estimator is a rough character-count heuristic, not a real tokenizer. Budget thresholds may fire too early or too late.
- **No authentication** — `API_KEY` is stored in plaintext environment variables.

**Potential improvements:**

- **Richer tool ecosystem** — add tools like `grep`, `find`, `git`, `web_fetch`, `web_search`, `code_search` to handle more complex workflows
- **Streaming output** — display LLM responses token-by-token for better perceived latency
- **Plugin architecture** — allow users to register custom tools at runtime via shared libraries or config files
- **Smarter context policies** — use actual tokenizers (tiktoken) and implement priority-based eviction (e.g. keep high-value tool results, drop low-info chat)
- **Multi-agent collaboration** — basic implementation done (`subagent_spawn`/`subagent_status`/`subagent_wait`), supports background parallel subtasks
- **Persistent memory** — implemented (`remember`/`recall`/`memory_delete`/`memory_update`), uses `.agent/memory/` index + typed files and injects only `preference`/`fact` on startup

---

## Try It Out

```bash
mkdir -p /tmp/agent-scratch && cd /tmp/agent-scratch
export API_KEY="your-key"
/path/to/build/c-agent
> write a C program that prints the first 20 primes to primes.c, then compile and run it
```

The agent will automatically write the file, compile it, run it, and report the output.
