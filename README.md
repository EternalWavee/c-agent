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
- **4 built-in tools** — `bash`, `read_file`, `write_file`, `edit_file`
- **Parallel execution** — read-only tools dispatched concurrently via pthreads
- **Context management** — automatic offload (large outputs to disk) and summary (LLM-based compression)
- **Session persistence** — save and restore conversations across runs
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
export API_KEY="your-api-key"
export MODEL_ID="qwen3coder"
export LLM_HOST="127.0.0.1"
export LLM_PORT="18080"
```

### Run

```bash
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
| `MODEL_ID` | `qwen3coder` | Model identifier |
| `LLM_HOST` | `127.0.0.1` | LLM server host |
| `LLM_PORT` | `18080` | LLM server port |
| `MAX_TOKENS` | `8000` | Max tokens per LLM response |
| `CONTEXT_WINDOW` | `8000` | Total token budget for conversation history |
| `OFFLOAD_THRESHOLD` | `0.8` | Fraction of window that triggers output offload |
| `SUMMARY_THRESHOLD` | `0.8` | Fraction of window that triggers history summarization |

---

## REPL Commands

| Command | Description |
|---------|-------------|
| `/model` | Interactive model picker |
| `/session` | Browse and restore saved sessions |
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

Read-only tools are automatically dispatched in parallel. State-changing tools run serially.

---

## Context Management

When the conversation grows too large, two policies reclaim space automatically:

1. **Offload** — moves large tool outputs to `.agent/offload/`, replacing them with compact placeholders that the LLM can use to recover the data via `read_file`
2. **Summary** — compresses older conversation history into a single summary message using the LLM itself

Both are triggered by token budget thresholds and run before each LLM request.

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
│   └── edit.c              # Substring replacement
├── ui/
│   ├── ui.c                # Event dispatch
│   └── render.c            # Terminal rendering thread
├── libs/
│   └── cJSON/              # JSON parser
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
- [x] Session save / restore
- [x] Terminal UI with spinner and per-tool status

### Planned

- [ ] **Evaluation harness** — end-to-end benchmark scenarios with metrics
- [ ] **SubAgent** — spawn child agents for independent subtasks
- [ ] **Memory** — project-level knowledge persistence across sessions
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
