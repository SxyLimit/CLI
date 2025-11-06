# mycli

一个演示用的交互式 CLI，展示了命令补全、路径类型约束、状态提示，以及与外部工具和大模型通信等功能。

## 编译与运行

```bash
g++ -std=c++17 main.cpp -o mycli
./mycli
```

程序会以原始模式读取键盘输入，可使用 `Ctrl+C` 或输入 `exit` / `quit` 退出。

需要配置如下 .env 文件：
```
LLM_API_KEY=your-api-key
LLM_BASE_URL=https://api.moonshot.cn/v1
LLM_MODEL=kimi-k2-turbo-preview
LLM_TEMPERATURE=0.6
HOME_PATH=settings
```

## 主要特性

- **命令注册与帮助**：通过 `ToolRegistry` 注册内置工具，可使用 `help` 查看命令说明。
- **路径补全与类型校验**：根据命令占位符或选项定义推断路径类型（文件/目录），补全时自动过滤；当输入不存在或类型不符时，以红色/黄色提示错误原因。
- **候选提示与 Ghost 文本**：在输入行下方展示至多三个候选项，输入末尾补全不存在时给出上下文提示。
- **状态栏扩展**：可通过 `StatusProvider` 注册自定义状态（示例中显示当前工作目录）。
- **外部工具配置**：支持在配置目录（默认 `./settings/`）下的 `mycli_tools.conf` 中用 INI 语法新增命令及子命令，含互斥选项、动态执行等。
- **消息提醒**：可监听指定目录（默认当前目录下的 `message/`）中的 `.md` 文件，新建或修改后提示符前会显示红色 `[M]`，通过 `message list/last/detail` 查看。
- **可定制提示符**：通过设置 `prompt.name` 与 `prompt.theme` 自定义提示符名称及颜色（支持蓝色与蓝紫渐变）。
- **LLM 接口**：提供 `llm` 命令，调用 `tools/llm.py` 通过 Moonshot(Kimi) 接口（或本地回显模式）完成调用与历史查看。

## 配置目录

- 默认情况下，所有配置文件存放在 `./settings/` 目录中，包括 `mycli_settings.json`、`mycli_tools.conf` 与 `mycli_llm_history.json`。
- 运行时可通过 `setting set home.path <目录>` 修改配置目录，CLI 会自动迁移已有文件并更新监听路径。
- 也可以在 `.env` 或系统环境变量中设置 `HOME_PATH=<目录>`，用于在启动前指定配置目录位置。

## 消息提醒与查看

1. 默认监听当前目录下的 `message/` 文件夹，可使用 `setting set message.folder <路径>` 改为其他目录。
2. 当监听目录中的 `.md` 文件新建或修改时，提示符前会出现红色 `[M]`，表示仍有未读内容。
3. 使用 `message list` 查看所有未读文件，`message last` 查看最近修改的文件内容，或通过 `message detail <文件名>` 定位并阅读指定文件。

若要取消监听，可运行 `setting set message.folder ""` 将路径清空。

## 提示符名称与主题

- `setting set prompt.name <名称>`：调整提示符名称，留空则恢复默认的 `mycli`。
- `setting set prompt.theme <blue|blue-purple>`：在纯蓝和蓝紫渐变主题之间切换，仅影响提示符名称部分。

## LLM 命令使用说明

`llm` 命令由 `tools/llm.py` 实现，提供与大模型的简单交互：

- `llm call <消息>`：异步发送文本到模型并立即返回，问答历史写入配置目录（默认 `./settings/mycli_llm_history.json`），等待模型返回后提示符前会出现红色 `[L]` 提醒。
- `llm recall`：查看最近一次调用的提示词与回复，同时清除 `[L]` 提醒。

默认使用 Moonshot 的 `kimi-k2-turbo-preview` 模型，并支持通过 `.env` 或环境变量覆盖以下配置：

| 变量名 | 说明 |
| --- | --- |
| `LLM_API_KEY` | 需要自行填写的 Moonshot API Key（默认为空）。|
| `LLM_BASE_URL` | API 地址，默认 `https://api.moonshot.cn/v1`。|
| `LLM_MODEL` | 模型名称，默认 `kimi-k2-turbo-preview`。|
| `LLM_SYSTEM_PROMPT` | 系统提示词，默认值与官方示例一致。|
| `LLM_TEMPERATURE` | 温度参数，默认 `0.6`。|

运行 `llm call` 时 CLI 会在后台调用 Python 脚本，若未设置密钥或缺少依赖，则脚本会退化为本地回显模式，方便调试。

当检测到新的历史记录时，提示符前会出现红色 `[L]` 提醒，可通过执行 `llm recall` 清除。

## 在配置文件中接入外部接口

`settings/mycli_tools.conf` 支持以 INI 语法扩展命令。要接入外部 HTTP、Python 或系统级接口，只需新增一个段落并指明执行方式：

```ini
[ai.translate]
summary=调用自定义翻译接口
type=python
exec=python3
script=./tools/ai_translate.py
options=--target-lang
positional=<text>
```

上述示例会在 CLI 中注册 `ai translate` 子命令，并通过 `python3 tools/ai_translate.py` 执行。脚本内部可以使用任意外部 SDK（例如请求自己的推理服务）。

如果需要调用系统命令或 HTTP 客户端，同样可以将 `type` 设置为 `system`，并在 `exec` 字段中填写可执行文件名称或绝对路径，CLI 会负责参数拼接与补全。例如：

```ini
[curl.get]
summary=GET 请求封装
type=system
exec=curl
options=-H,--data
positional=<url>
```

修改配置文件后，重新启动 `mycli` 即可加载新命令，也可在运行时使用 `reload` 命令重新读取配置。

## 目录结构

- `main.cpp`：REPL 主循环、渲染及补全逻辑。
- `globals.hpp`：公共类型、辅助函数与全局状态声明。
- `tools.hpp`：路径补全实现、命令/状态注册、动态工具加载。
- `tools/pytool.py`：示例 Python 工具脚本。
- `settings/`：默认配置目录，包含 `mycli_settings.json`、`mycli_tools.conf`、`mycli_llm_history.json`。

## 开发提示

- 所有可执行文件均需使用 C++17 编译。
- 若新增命令，记得在 `register_all_tools()` 中注册并为选项配置合适的路径类型。
- 自定义补全策略时，可复用 `pathCandidatesForWord` 与 `analyzePositionalPathContext` 等辅助函数。
