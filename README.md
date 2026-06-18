<h1 align="center">C Agent</h1>

<p align="center">
  <img src="https://img.shields.io/badge/CS2313-操作系统课程设计-blueviolet?style=flat-square" alt="CS2313"/>
  <img src="https://img.shields.io/badge/language-C11-blue?style=flat-square&logo=c" alt="C11"/>
  <img src="https://img.shields.io/badge/platform-Linux-lightgrey?style=flat-square&logo=linux" alt="Linux"/>
  <img src="https://img.shields.io/badge/build-Make-green?style=flat-square&logo=gnu" alt="Make"/>
</p>

<p align="center">
  一个轻量级的、基于终端的编程智能体，纯 C 实现。<br/>
  <a href="README_en.md">English</a> · <a href="report.pdf">课程报告</a> · <a href="appendix.pdf">附录</a>
</p>

C Agent 是一个基于 LLM 工具调用的终端编程助手。用户用自然语言描述任务，Agent 自动调用工具（执行 shell 命令、读写文件、编辑代码）来完成。核心特性包括 ReAct 多轮推理、只读工具并发执行、上下文自动管理（卸载 + 摘要）、工作区沙箱隔离。本项目为上海交通大学 CS2313 操作系统课程设计。

---

## 功能特性

- **ReAct 推理循环** — 多轮工具调用，直到任务完成
- **16 个内置工具** — `bash`、`read_file`、`write_file`、`edit_file`、`remember`、`recall`、`memory_delete`、`memory_update`、`subagent_spawn`、`subagent_status`、`subagent_wait`、`current_time`、`load_skill`、`web_fetch`、`web_search`、`install_skill`
- **并发执行** — 只读工具通过 pthread 并发调度
- **上下文管理** — 自动卸载（会话隔离 + manifest）和结构化摘要（LLM handoff）
- **会话持久化** — append-only 日志 + checkpoint 架构，崩溃恢复，自动命名，交互式会话管理
- **项目记忆** — `.agent/memory/` 索引 + 六类 typed files，跨会话保存项目知识
- **沙箱安全** — 所有文件路径限制在工作目录内
- **危险命令过滤** — 执行前拦截高危 shell 命令
- **终端 UI** — 实时 spinner、工具状态显示和 Markdown 终端渲染

---

## 快速开始

依赖：C 编译器、`make`、POSIX shell、`caddy`。`start.sh` 会启动本地代理到 SJTU 模型服务。

> 先将[start.sh](./start.sh)的`dummy`改成申请到的api_key 

### Linux

```bash
sudo apt install -y build-essential make caddy
./start.sh
```

### macOS

```bash
xcode-select --install
brew install caddy
./start.sh
```

### Windows

原生 Windows 暂不支持，推荐 WSL2：

```powershell
wsl --install
```

进入 WSL 后按 Linux 步骤运行。

手动启动方式：

```bash
make
export API_KEY="SJTU-API密钥"
caddy reverse-proxy --from :18080 --to https://models.sjtu.edu.cn --change-host-header
# 另开终端
./build/c-agent
```

---

## 里程碑

> 🚀 **2026.05.27** — c-agent 初具雏形，具备完整的工具调用、记忆管理和自主编码能力。基于我们搭建的 C 框架，配合上海交通大学提供的 DeepSeek 模型，仅通过自然语言指令即完成了[本次迭代](https://github.com/EternalWavee/c-agent/commit/2b2475aadaeb43fde3a1b946ea977ec38496a56c)。

---

## 环境变量

| 变量 | 默认值 | 说明 |
|------|--------|------|
| `API_KEY` | `none` | LLM 认证的 Bearer Token |
| `MODEL_ID` | `deepseek-chat` | 模型名称 |
| `LLM_HOST` | `127.0.0.1` | LLM 服务地址 |
| `LLM_PORT` | `18080` | LLM 服务端口 |
| `MAX_TOKENS` | `8000` | 单次回复最大 token 数 |
| `CONTEXT_WINDOW` | `32000` | 上下文窗口总预算 |
| `OFFLOAD_THRESHOLD` | `0.8` | 触发卸载策略的窗口比例 |
| `SUMMARY_THRESHOLD` | `0.8` | 触发摘要策略的窗口比例 |

---

## REPL 命令

| 命令 | 说明 |
|------|------|
| `/model` | 交互式切换模型 |
| `/session` | 浏览并恢复历史会话（交互式选择器） |
| `/session new` | 创建新会话 |
| `/session name X` | 重命名当前会话 |
| `/session delete X` | 删除指定会话 |
| `/session restore X` | 按 ID 恢复会话 |
| `/memory` | 触发离线 memory consolidation，将工具观察结果提炼为项目记忆条目 |
| `/remember` | 让主 Agent 回顾当前对话，主动调用 `remember` 保存值得长期保存的信息 |
| `/help` | 显示帮助信息 |
| `exit` / `quit` / `q` | 退出 |

---

## 内置工具

| 工具 | 说明 | 只读 |
|------|------|------|
| `bash` | 执行 shell 命令 | 否 |
| `read_file` | 读取文件内容 | 是 |
| `write_file` | 写入文件 | 否 |
| `edit_file` | 替换文件中的首次匹配文本 | 否 |
| `remember` | 将知识存入持久化项目记忆 | 否 |
| `recall` | 读取项目记忆（支持按关键词/类型过滤） | 是 |
| `memory_delete` | 按索引删除记忆条目 | 否 |
| `memory_update` | 按索引更新记忆条目 | 否 |
| `subagent_spawn` | 启动后台子智能体执行独立子任务 | 否 |
| `subagent_status` | 查看所有子智能体状态 | 是 |
| `subagent_wait` | 等待子智能体完成并获取结果 | 否 |
| `current_time` | 获取当前系统时间 | 是 |
| `load_skill` | 读取 `.agent/skills/<name>/SKILL.md` 或 `~/.c-agent/skills/<name>/SKILL.md` 完整技能说明 | 是 |
| `web_fetch` | 抓取外部 `http/https` URL，禁止 localhost/private network | 是 |
| `web_search` | 用 Bing RSS 搜索网页，返回标题、URL 和摘要候选 | 是 |
| `install_skill` | 从本地目录、git repo 或 GitHub tree URL 安装 skill package | 否 |

只读工具自动并发执行，写工具串行执行。

---

## 工具生命周期钩子

`hooks.c` 实现了一个轻量的发布-订阅机制，在工具调用的两个时间点触发回调：

- **`HOOK_BEFORE_TOOL`** — 工具执行前触发，此时 `result` 字段为 NULL
- **`HOOK_AFTER_TOOL`** — 工具执行后触发，携带完整的输入参数和输出结果

调用方（`tools/executor.c`）在每次并发或串行调度工具时都会 emit 这两个事件；订阅方通过 `hooks_register` 注册自己的回调函数，最多每个事件 16 个。

目前注册的唯一内置钩子是 **`memory_tool_hook`**：在每次工具执行后把 `(工具名, 参数, 输出)` 写入短期观察缓冲区（`memory_observe`）。这是 `/memory` 命令的数据来源——用户执行 `/memory` 时，consolidation 流程读取这批观察记录，让 LLM 从中提炼值得长期保存的项目知识并写入 `.agent/memory/`。

---



当对话历史过长时，两个策略会在每次 LLM 请求前自动回收空间：

1. **卸载策略（offload）** — 将大型工具输出写入 `.agent/offload/`，并把原 tool message 替换为短 preview + 结构化引用。交互式会话按 session 隔离：`.agent/offload/<session_id>/<n>.txt`；非交互/测试模式保持 `.agent/offload/<n>.txt`。
2. **摘要策略（summary）** — 使用 LLM 将旧对话压缩为一条结构化 handoff 消息，保留 `Goal`、`Completed`、`Current State`、`Key Files`、`Decisions`、`Offloaded References`、`Open Questions`、`Next Steps` 等信息，并保留最近 `KEEP_RECENT_MSGS` 条原始消息。

卸载后的占位符包含：

```text
[OFFLOADED_TOOL_OUTPUT]
id: 0
path: .agent/offload/<session_id>/0.txt
size_bytes: ...
manifest: .agent/offload/<session_id>/MANIFEST.md
recovery: call read_file ...
```

每个 offload 目录还维护 `MANIFEST.md`，记录每个卸载文件的来源工具、参数、大小和 preview，方便 Agent 后续通过 `read_file` 了解“之前做过什么、哪些输出被卸载了”。Agent 的 system prompt 明确要求：如果回答或下一步动作依赖被卸载内容，必须先读取对应文件，不应只凭 preview 猜测。

两个策略由 token 预算阈值触发。offload 先运行，summary 后运行；如果 offload 已经把 token 降到阈值以下，summary 不会额外触发。

---

## 会话系统

每个会话存储为 `.agent/sessions/<id>/` 下的目录：

```
.agent/sessions/
  20260524_093000/
    meta.json            # 名称、模型、时间戳、消息数
    checkpoint.json      # 全量消息快照（原子写入）
    log.jsonl            # checkpoint 之后的追加日志
```

- 每轮对话后自动追加消息到 `log.jsonl`
- 每 10 条新消息自动写一次 checkpoint 并截断日志
- 启动时默认创建新会话，用 `/session` 可恢复历史会话
- 会话自动以最后一条用户消息命名
- 崩溃安全：恢复时跳过残损的日志行

---

## 联网能力

`web_fetch` 提供最小联网能力：抓取明确的 `http/https` URL，跟随重定向，限制响应大小和超时，并拒绝 localhost/private network 地址。它适合读取官方文档、远程 `SKILL.md`、GitHub raw 文件等。

`web_search` 提供简单搜索能力：直接请求 Bing 的 `format=rss` 搜索结果并解析前几个条目。它不使用 Bing API，RSS 不可用时应换查询或使用已知 URL。

`install_skill` 用于安装技能包，支持本地目录、git repo URL 和 GitHub tree URL。安装过程只复制包含 `SKILL.md` 的目录，不会自动执行 `scripts/`。已存在技能默认拒绝覆盖，除非传入 `force=true`。

---

## 技能系统

技能用于按需加载特定任务的操作说明，避免把所有长 prompt 都塞进 system prompt。目录格式：

```text
项目级：.agent/skills/<skill-name>/
用户级：~/.c-agent/skills/<skill-name>/
  SKILL.md          # 必须，技能主文件
  scripts/          # 可选，辅助脚本
  references/       # 可选，参考文档
  assets/           # 可选，模板/资源
```

启动时 Agent 会扫描项目级 `.agent/skills/` 和用户级 `~/.c-agent/skills/` 下的 `SKILL.md` metadata，生成很短的 skill manifest。同名时项目级 skill 覆盖用户级 skill。支持 frontmatter、markdown 表格或简单 `key value` 形式，常用字段：

```text
name: benchmark-research-skill
description: Use when analyzing benchmarks, datasets, metrics, baselines, or related work.
allowed-tools: Read, Write, Edit, Bash, WebFetch, Grep, Glob
```

当某个技能相关时，Agent 调用 `load_skill {"name":"..."}` 读取完整 `SKILL.md`。用户也可以要求 Agent 用 `install_skill` 从本地目录、git repo 或 GitHub tree URL 安装技能；默认安装到项目级 `.agent/skills/`，`scope=user` 时安装到 `~/.c-agent/skills/`。`scripts/`、`references/`、`assets/` 第一版只作为普通文件目录支持；脚本执行仍通过现有 `bash` 工具完成，暂不做专门的 `run_skill_script`。

---

## 记忆系统

Agent 在 `.agent/memory/` 中维护跨会话的项目知识。存储结构从单一 markdown 文件改为索引 + 六类 typed files：

```text
.agent/memory/
  MEMORY.md          # 索引，列出各类 memory 文件
  pattern.md         # 代码模式/约定
  preference.md      # 用户偏好
  architecture.md    # 架构知识
  bug.md             # 已知问题
  workflow.md        # 构建/测试/运行流程
  fact.md            # 稳定事实
```

**工作流程：**

```text
启动时：preference.md + fact.md → 注入 system prompt，让 Agent 记得用户身份、偏好和稳定事实
任务中：Agent 可调用 recall 按 keyword/type 查询完整项目记忆
写入时：remember 根据 type 写入对应 typed file，并做精确去重
维护时：memory_delete/memory_update 使用 recall 返回的全局 index 修改条目
```

旧的 `.agent/memory.md` 如果存在，会在启动时迁移其中的 `- [type] content` 条目到新目录结构。退出时的两步 LLM consolidation 已禁用；后续计划用受限 memory subagent 来整理 `.agent/memory/`，避免退出时阻塞或把格式改坏。

**记忆分类（6 种）：**

| 类型 | 含义 | 例子 |
|------|------|------|
| `pattern` | 代码模式/命名习惯 | "项目用 snake_case 命名函数" |
| `preference` | 用户偏好 | "用户喜欢简洁的注释" |
| `architecture` | 架构知识 | "agent 通过 TCP 直连调用 LLM" |
| `bug` | 已知问题 | "session_load 不恢复 model 字段" |
| `workflow` | 工作流程 | "构建用 make，测试用 make test" |
| `fact` | 一般事实 | "项目是 CS2313 课程设计" |

**写入方式：**
- Agent 会主动调用 `remember`，不必等用户明确说“记住”
- 应主动记忆稳定用户事实、长期偏好、项目固定流程、架构决策、反复出现的 bug/fix、用户明确纠正的信息
- 不应记忆一次性命令输出、临时报错、未验证搜索结果、短期计划或玩笑话，除非用户确认重要
- `/memory` 会让 Agent 回顾当前对话，并对值得长期保存的信息逐条调用 `remember`
- 用户可以手动编辑 `.agent/memory/*.md`

**读取方式：**
- 启动时只自动注入 `preference` 和 `fact`，避免把完整项目记忆一股脑塞进 system prompt
- Agent 调用 `recall` 工具读取完整记忆（支持 `keyword` 关键词过滤和 `type` 类型过滤）

**修改/删除：**
- `recall` 返回带 `[index]` 索引的记忆列表
- Agent 调用 `memory_delete <index>` 删除指定条目
- Agent 调用 `memory_update <index> <content>` 更新指定条目

---

## 运行测试

```bash
make test               # 全部集成测试
make test-cunit         # C 单元测试（注册表、沙箱、卸载、摘要）
make test-asan          # AddressSanitizer 全量测试
make test-tsan          # ThreadSanitizer 并发测试
```

---

## 项目结构

```
.
├── main.c                  # 入口和 REPL
├── Makefile
├── start.sh                # 一键启动：caddy 代理 + agent
├── report.pdf              # 课程设计报告
├── appendix.pdf            # 附录
├── core/                   # 核心模块
│   ├── config.c/h          # 环境变量配置
│   ├── message.c/h         # 消息结构
│   ├── session.c/h         # 会话持久化（日志 + checkpoint）
│   ├── memory.c/h          # 项目记忆（.agent/memory/ 索引 + typed files）
│   ├── skills.c/h          # 技能扫描和 SKILL.md 加载
│   ├── hooks.c/h           # 工具生命周期钩子（before/after tool）
│   ├── cmd.c/h             # 斜杠命令分发
│   ├── http.c/h            # HTTP 客户端
│   └── util.c/h            # 通用工具函数
├── agent/
│   ├── agent.c             # 核心对话循环（ReAct）
│   ├── prompt.c            # System prompt 拼装（规则、时间、memory 注入）
│   └── llm_client.c        # HTTP 传输 + JSON 协议
├── context/
│   ├── context.c           # 上下文预算和策略引擎
│   ├── policy_offload.c    # 无损卸载到磁盘（会话隔离 + manifest）
│   └── policy_summary.c    # 有损压缩（结构化 LLM handoff）
├── tools/
│   ├── registry.c          # 静态工具注册表
│   ├── init.c              # 工具注册初始化
│   ├── executor.c          # 并发/串行调度器
│   ├── sandbox.c           # 工作区路径沙箱
│   ├── bash.c              # Shell 命令执行
│   ├── read.c              # 文件读取
│   ├── write.c             # 文件写入
│   ├── edit.c              # 子串替换
│   ├── memory_tool.c       # remember + recall + delete + update 工具
│   ├── skill_tool.c        # load_skill + install_skill 工具
│   ├── subagent.c          # 后台子智能体（spawn/status/wait）
│   ├── web_fetch.c         # HTTP/HTTPS 抓取
│   ├── web_search.c        # Bing RSS 搜索
│   └── current_time.c      # 当前系统时间
├── ui/
│   ├── ui.c                # 事件分发
│   ├── render.c            # 终端渲染线程
│   └── markdown.c          # Markdown 终端渲染（md4c 封装）
└── libs/
    ├── cJSON.c / cJSON.h   # JSON 解析库
    └── md4c.c / md4c.h     # Markdown 解析库
```

---

## 开发进度

### 已完成

- [x] 单轮和多轮工具调用（ReAct 循环）
- [x] 静态工具注册表
- [x] 只读工具并发执行
- [x] 工作区沙箱（路径逃逸防护）
- [x] 危险命令过滤
- [x] 上下文卸载（大输出存盘）
- [x] 上下文摘要（LLM 压缩历史）
- [x] 会话持久化（append-only 日志 + checkpoint，崩溃恢复，自动命名）
- [x] 项目记忆（.agent/memory/ 索引 + 六类 typed files），支持过滤、删除、更新
- [x] 终端 UI（spinner + 工具状态 + Markdown 渲染）
- [x] 子智能体（后台并行执行独立子任务）

### 计划中

- [ ] **评测框架** — 端到端场景基准测试与指标收集
- [ ] **流式输出** — 逐 token 显示 LLM 响应，降低感知延迟
- [ ] **插件架构** — 支持用户通过共享库或配置文件在运行时注册自定义工具

---

## 不足与改进方向

**当前不足：**

- **内置工具较少** — 目前已有 16 个工具，基础工具（`bash`、`read_file`、`write_file`、`edit_file`）+ 记忆管理（`remember`/`recall`/`memory_delete`/`memory_update`）+ 子智能体（`subagent_spawn`/`subagent_status`/`subagent_wait`）+ 联网（`web_fetch`/`web_search`）+ 技能系统（`load_skill`/`install_skill`）+ `current_time`；缺少代码索引、diff 查看、git 等常见能力
- **无流式输出** — LLM 响应需完整接收后才显示，长回复时体验较差
- **单一模型** — 不支持灵活切换 LLM 提供商或为不同任务混合使用模型
- **Token 估算粗糙** — 使用字符数近似估算，非真实 tokenizer，预算阈值可能过早或过晚触发
- **密钥明文存储** — `API_KEY` 以明文形式存于环境变量

**改进方向：**

- **丰富工具生态** — 新增 `grep`、`find`、`git`、`code_search` 等工具，覆盖更复杂的工作流
- **流式输出** — 逐 token 显示 LLM 响应，降低感知延迟
- **插件架构** — 支持用户通过共享库或配置文件在运行时注册自定义工具
- **更智能的上下文策略** — 使用真实 tokenizer（如 tiktoken），实现基于优先级的淘汰（保留高价值工具结果，丢弃低信息量对话）
- **多智能体协作** — 已实现基础版本（`subagent_spawn`/`subagent_status`/`subagent_wait`），支持后台并行子任务
- **持久化记忆** — 已实现（`remember`/`recall`/`memory_delete`/`memory_update`），采用 `.agent/memory/` 索引 + typed files，启动时只注入 `preference`/`fact`

---

## 实际体验

```bash
mkdir -p /tmp/agent-scratch && cd /tmp/agent-scratch
export API_KEY="你的密钥"
/path/to/build/c-agent
> 帮我写一个 C 程序输出前 20 个质数，保存为 primes.c，然后编译运行
```

Agent 会自动完成：写文件 → 编译 → 运行 → 汇报结果。

---

## 致谢

- [cJSON](https://github.com/DaveGamble/cJSON) — 项目使用的轻量级 C JSON 解析库。
- [MD4C](https://github.com/mity/md4c) — 项目使用的 Markdown 解析库，帮助 C Agent 将模型回复渲染为更清晰的终端输出。感谢 MD4C 作者 Martin Mitáš 及贡献者。
