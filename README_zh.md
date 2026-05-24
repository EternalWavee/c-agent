<h1 align="center">C Agent</h1>

<p align="center">
  <img src="https://img.shields.io/badge/CS2313-操作系统课程设计-blueviolet?style=flat-square" alt="CS2313"/>
  <img src="https://img.shields.io/badge/language-C11-blue?style=flat-square&logo=c" alt="C11"/>
  <img src="https://img.shields.io/badge/platform-Linux-lightgrey?style=flat-square&logo=linux" alt="Linux"/>
  <img src="https://img.shields.io/badge/build-Make-green?style=flat-square&logo=gnu" alt="Make"/>
</p>

<p align="center">
  一个轻量级的、基于终端的编程智能体，纯 C 实现。<br/>
  <a href="README.md">English</a>
</p>

C Agent 是一个基于 LLM 工具调用的终端编程助手。用户用自然语言描述任务，Agent 自动调用工具（执行 shell 命令、读写文件、编辑代码）来完成。核心特性包括 ReAct 多轮推理、只读工具并发执行、上下文自动管理（卸载 + 摘要）、工作区沙箱隔离。本项目为上海交通大学 CS2313 操作系统课程设计。

---

## 功能特性

- **ReAct 推理循环** — 多轮工具调用，直到任务完成
- **4 个内置工具** — `bash`、`read_file`、`write_file`、`edit_file`
- **并发执行** — 只读工具通过 pthread 并发调度
- **上下文管理** — 自动卸载（大输出存盘）和摘要（LLM 压缩历史）
- **会话持久化** — append-only 日志 + checkpoint 架构，崩溃恢复，自动命名，交互式会话管理
- **沙箱安全** — 所有文件路径限制在工作目录内
- **危险命令过滤** — 执行前拦截高危 shell 命令
- **终端 UI** — 实时 spinner 和工具状态显示

---

## 快速开始

### 环境要求

- GCC 或 Clang（支持 C11）
- GNU Make
- 兼容 OpenAI API 格式的 LLM 端点

### 构建

```bash
make            # 开发构建
make asan       # AddressSanitizer + UBSan
make tsan       # ThreadSanitizer
```

### 配置

```bash
export API_KEY="你的API密钥"
export MODEL_ID="deepseek-chat"
export LLM_HOST="127.0.0.1"
export LLM_PORT="18080"
```

### 运行

```bash
# 使用 caddy 代理（推荐）
./start.sh

# 或手动启动
caddy reverse-proxy --from :18080 --to https://models.sjtu.edu.cn --change-host-header &
./build/c-agent
```

```
C Agent - /home/user/project
Model: qwen3coder
Type 'exit' to quit.
> 列出当前目录下所有 .c 文件并统计行数
```

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

只读工具自动并发执行，写工具串行执行。

---

## 上下文管理

当对话历史过长时，两个策略自动回收空间：

1. **卸载策略** — 将大型工具输出移至 `.agent/offload/`，替换为紧凑占位符，LLM 可通过 `read_file` 恢复数据
2. **摘要策略** — 使用 LLM 将旧对话历史压缩为单条摘要消息

两个策略由 token 预算阈值触发，在每次 LLM 请求前执行。

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
├── session.c               # 会话持久化（日志 + checkpoint）
├── cmd.c                   # 斜杠命令分发
├── config.c                # 环境变量配置
├── agent/
│   ├── agent.c             # 核心对话循环（ReAct）
│   └── llm_client.c        # HTTP 传输 + JSON 协议
├── context/
│   ├── context.c           # 上下文预算和策略引擎
│   ├── policy_offload.c    # 无损卸载到磁盘
│   └── policy_summary.c    # 有损压缩（LLM 摘要）
├── tools/
│   ├── registry.c          # 静态工具注册表
│   ├── executor.c          # 并发/串行调度器
│   ├── sandbox.c           # 工作区路径沙箱
│   ├── bash.c              # Shell 命令执行
│   ├── read.c              # 文件读取
│   ├── write.c             # 文件写入
│   └── edit.c              # 子串替换
├── ui/
│   ├── ui.c                # 事件分发
│   └── render.c            # 终端渲染线程
├── libs/
│   └── cJSON/              # JSON 解析库
├── start.sh                # 一键启动：caddy 代理 + agent
└── Makefile
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
- [x] 终端 UI（spinner + 工具状态）

### 计划中

- [ ] **评测框架** — 端到端场景基准测试与指标收集
- [ ] **子智能体** — 为独立子任务生成子 Agent
- [ ] **记忆系统** — 跨会话的项目级知识持久化
- [ ] **技能系统** — 按需注入特定任务的 prompt

---

## 不足与改进方向

**当前不足：**

- **内置工具较少** — 仅有 4 个工具（`bash`、`read_file`、`write_file`、`edit_file`），缺少 Web 搜索、代码索引、diff 查看、git 操作等常见能力
- **无流式输出** — LLM 响应需完整接收后才显示，长回复时体验较差
- **单一模型** — 不支持灵活切换 LLM 提供商或为不同任务混合使用模型
- **Token 估算粗糙** — 使用字符数近似估算，非真实 tokenizer，预算阈值可能过早或过晚触发
- **密钥明文存储** — `API_KEY` 以明文形式存于环境变量

**改进方向：**

- **丰富工具生态** — 新增 `grep`、`find`、`git`、`web_fetch`、`code_search` 等工具，覆盖更复杂的工作流
- **流式输出** — 逐 token 显示 LLM 响应，降低感知延迟
- **插件架构** — 支持用户通过共享库或配置文件在运行时注册自定义工具
- **更智能的上下文策略** — 使用真实 tokenizer（如 tiktoken），实现基于优先级的淘汰（保留高价值工具结果，丢弃低信息量对话）
- **多智能体协作** — 主 Agent 将子任务委托给具有独立上下文的专用子 Agent
- **持久化记忆** — 存储项目级知识（代码结构、编码规范、历史决策），跨会话保留

---

## 实际体验

```bash
mkdir -p /tmp/agent-scratch && cd /tmp/agent-scratch
export API_KEY="你的密钥"
/path/to/build/c-agent
> 帮我写一个 C 程序输出前 20 个质数，保存为 primes.c，然后编译运行
```

Agent 会自动完成：写文件 → 编译 → 运行 → 汇报结果。
