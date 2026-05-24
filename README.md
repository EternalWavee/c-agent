# C Agent

一个基于 LLM 工具调用的终端编程助手，纯 C 实现。上海交通大学 CS2313 操作系统课程设计。

<a href="README_en.md">English</a>

## 快速开始

```bash
./start.sh    # 自动构建 + 启动 caddy 代理 + 运行 agent
```

首次使用前配置环境变量：

```bash
export API_KEY="你的API密钥"
export MODEL_ID="deepseek-chat"
export LLM_HOST="127.0.0.1"
export LLM_PORT="18080"
```

## 功能特性

- **ReAct 推理循环** — 多轮工具调用，直到任务完成
- **6 个内置工具** — `bash`、`read_file`、`write_file`、`edit_file`、`remember`、`recall`
- **并发执行** — 只读工具通过 pthread 并发调度
- **上下文管理** — 自动卸载（大输出存盘）和摘要（LLM 压缩历史）
- **会话持久化** — append-only 日志 + checkpoint，崩溃恢复，自动命名
- **项目记忆** — 短期→长期记忆整合，LLM 自动去重和分类
- **沙箱安全** — 文件路径限制在工作目录内
- **危险命令过滤** — 执行前拦截高危 shell 命令

## REPL 命令

| 命令 | 说明 |
|------|------|
| `/model` | 交互式切换模型 |
| `/session` | 浏览并恢复历史会话 |
| `/session new` | 创建新会话 |
| `/session name X` | 重命名当前会话 |
| `/session delete X` | 删除指定会话 |
| `/session restore X` | 按 ID 恢复会话 |
| `/help` | 显示帮助信息 |
| `exit` / `quit` / `q` | 退出 |

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
- 每 10 条新消息自动 checkpoint 并截断日志
- 启动时默认创建新会话，用 `/session` 恢复历史会话
- 会话自动以最后一条用户消息命名
- 崩溃安全：恢复时跳过残损的日志行

## 记忆系统

借鉴 agentmemory 的短期→长期记忆架构，Agent 在 `.agent/memory.md` 中维护跨会话的项目知识。

### 工作流程

```
会话中：工具调用 → 自动生成短期记忆（内存缓冲区）
退出时：短期记忆 → LLM#1 提取知识 → LLM#2 与旧记忆合并去重 → memory.md
启动时：memory.md → 注入 system prompt → Agent "记得"项目知识
```

### 记忆分类

| 类型 | 含义 | 例子 |
|------|------|------|
| `pattern` | 代码模式/命名习惯 | "项目用 snake_case 命名函数" |
| `preference` | 用户偏好 | "用户喜欢简洁的注释" |
| `architecture` | 架构知识 | "agent 通过 TCP 直连调用 LLM" |
| `bug` | 已知问题 | "session_load 不恢复 model 字段" |
| `workflow` | 工作流程 | "构建用 make，测试用 make test" |
| `fact` | 一般事实 | "项目是 CS2313 课程设计" |

### 两步整合

1. **LLM#1 提取** — 从短期记忆（工具调用观察）中提取结构化知识
2. **LLM#2 合并** — 新记忆 + 旧 memory.md → 去重、更新过时条目、按分类整理

### 写入方式

- Agent 自动捕获工具调用（`write_file`、`edit_file`、`bash`、`read_file`）
- Agent 主动调用 `remember` 工具保存重要发现
- 会话退出时自动触发整合

### 读取方式

- system prompt 自动注入 memory.md 内容
- Agent 调用 `recall` 工具读取完整记忆

## 环境变量

| 变量 | 默认值 | 说明 |
|------|--------|------|
| `API_KEY` | `none` | LLM 认证 Token |
| `MODEL_ID` | `deepseek-chat` | 模型名称 |
| `LLM_HOST` | `127.0.0.1` | LLM 服务地址 |
| `LLM_PORT` | `18080` | LLM 服务端口 |
| `MAX_TOKENS` | `8000` | 单次回复最大 token 数 |
| `CONTEXT_WINDOW` | `32000` | 上下文窗口总预算 |
| `OFFLOAD_THRESHOLD` | `0.8` | 触发卸载的窗口比例 |
| `SUMMARY_THRESHOLD` | `0.8` | 触发摘要的窗口比例 |

## 测试

```bash
make test-cunit-offload   # 卸载策略单元测试
make test-cunit-summary   # 摘要策略单元测试
make test-ca              # Phase CA 集成测试
make test-cb              # Phase CB 集成测试
make test-asan            # AddressSanitizer 全量测试
make test-tsan            # ThreadSanitizer 并发测试
```

## 项目结构

```
.
├── main.c                  # 入口和 REPL
├── session.c               # 会话持久化（日志 + checkpoint）
├── memory.c                # 记忆系统（短期→长期整合）
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
│   ├── edit.c              # 子串替换
│   └── memory_tool.c       # remember + recall 工具
├── ui/
│   ├── ui.c                # 事件分发
│   └── render.c            # 终端渲染线程
├── libs/
│   └── cJSON/              # JSON 解析库
├── start.sh                # 一键启动
└── Makefile
```
