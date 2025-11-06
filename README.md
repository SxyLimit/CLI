# mycli

一个演示用的交互式 CLI，展示了命令补全、路径类型约束、状态提示，以及与外部工具和大模型通信等功能。

## 编译与运行

```bash
g++ -std=c++17 main.cpp -o mycli
./mycli
```

程序会以原始模式读取键盘输入，可使用 `Ctrl+C` 或输入 `exit` / `quit` 退出。

## 主要特性

- **命令注册与帮助**：通过 `ToolRegistry` 注册内置工具，可使用 `help` 查看命令说明。
- **路径补全与类型校验**：根据命令占位符或选项定义推断路径类型（文件/目录），补全时自动过滤；当输入不存在或类型不符时，以红色/黄色提示错误原因。
- **候选提示与 Ghost 文本**：在输入行下方展示至多三个候选项，输入末尾补全不存在时给出上下文提示。
- **状态栏扩展**：可通过 `StatusProvider` 注册自定义状态（示例中显示当前工作目录）。
- **外部工具配置**：支持在 `mycli_tools.conf` 中用 INI 语法新增命令及子命令，含互斥选项、动态执行等。
- **消息提醒**：可以对指定文件夹中新建的 `.md` 文件进行监控，未读消息会在提示符前显示红色星标，通过 `message` 命令查看。
- **LLM 接口**：提供 `llm` 命令，调用 `tools/llm.py` 通过 OpenAI 接口（或本地回显模式）完成调用与历史查看。

## 消息提醒与查看

1. 使用 `setting set message.folder <路径>` 指定需要监听的目录。
2. 当该目录中新建 `.md` 文件时，提示符前会出现红色 `★`，表示有未读消息。
3. 执行 `message` 命令即可依次查看新文件内容，阅读后红星会自动消失。

若要取消监听，可运行 `setting set message.folder ""` 将路径清空。

## LLM 命令使用说明

`llm` 命令由 `tools/llm.py` 实现，提供与大模型的简单交互：

- `llm call <消息>`：发送文本到模型并打印回复，同时将问答历史保存到 `~/.mycli_llm_history.json`。
- `llm recall`：查看最近一次调用的提示词与回复。

默认使用 `gpt-4o-mini` 模型，可通过环境变量 `MYCLI_LLM_MODEL` 自定义；使用 OpenAI 接口时需配置 `OPENAI_API_KEY`。若未设置密钥或缺少依赖，则脚本会退化为本地回显模式，方便调试。

## 在配置文件中接入外部接口

`mycli_tools.conf` 支持以 INI 语法扩展命令。要接入外部 HTTP、Python 或系统级接口，只需新增一个段落并指明执行方式：

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
- `mycli_tools.conf`：外部工具配置示例。

## 开发提示

- 所有可执行文件均需使用 C++17 编译。
- 若新增命令，记得在 `register_all_tools()` 中注册并为选项配置合适的路径类型。
- 自定义补全策略时，可复用 `pathCandidatesForWord` 与 `analyzePositionalPathContext` 等辅助函数。
