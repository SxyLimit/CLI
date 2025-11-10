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
- **路径补全与类型校验**：根据命令占位符或选项定义推断路径类型（文件/目录），补全时自动过滤；支持为文件参数声明允许的后缀（例如 `.climg`），同时在帮助文档与错误提示中给出明确指引。
- **命令委托**：`run <command> [args...]` 会在当前 shell 中执行任意系统命令，参数使用 `shellEscape` 逐项转义，便于临时调用外部工具。
- **候选提示与 Ghost 文本**：在输入行下方展示至多三个候选项，输入末尾补全不存在时给出上下文提示。
- **状态栏扩展**：可通过 `StatusProvider` 注册自定义状态（示例中显示当前工作目录）。
- **外部工具配置**：支持在配置目录（默认 `./settings/`）下的 `mycli_tools.conf` 中用 INI 语法新增命令及子命令，含互斥选项、动态执行等。
- **消息提醒**：可监听指定目录（默认当前目录下的 `message/`）中的 `.md` 文件，新建或修改后提示符前会显示红色 `[M]`，通过 `message list/last/detail` 查看。
- **可定制提示符**：通过设置 `prompt.name` 与 `prompt.theme` 自定义提示符名称及颜色。提供纯蓝、蓝紫、红黄渐变与紫橙渐变四种主题，并可为任意主题配置结构化图片（`prompt.theme_art_path.<theme>`）以在 `show MyCLI` 中输出彩色图案。
- **LLM 接口**：提供 `llm` 命令，调用 `tools/llm.py` 通过 Moonshot(Kimi) 接口（或本地回显模式）完成调用与历史查看。

## 配置目录

- 默认情况下，所有配置文件存放在 `./settings/` 目录中，包括 `mycli_settings.conf`、`mycli_tools.conf` 与 `mycli_llm_history.json`。
- `mycli_settings.conf` 采用逐行 `key=value` 的纯文本格式，方便手动编辑；不要误认为是 JSON 文件。
- 运行时可通过 `setting set home.path <目录>` 修改配置目录，CLI 会自动迁移已有文件并更新监听路径。
- 也可以在 `.env` 或系统环境变量中设置 `HOME_PATH=<目录>`，用于在启动前指定配置目录位置。

## 设置命令

- `setting get [分段…]`：不带参数时列出所有配置项；指定某个分支前缀时，会输出该子树下的所有键和值，传入完整键则展示对应的单项。
- `setting set <分段…> <值>`：修改某个完整键的值，支持根据键类型提供布尔、枚举、文件路径等自动补全提示（输入 `/`、`./` 后可继续补全路径）。

每一级分段都可以使用补全按键查看可用的下一层节点，确保在输入 `set` 时始终能够补全到最终叶子节点后再录入新值。

## 消息提醒与查看

1. 默认监听当前目录下的 `message/` 文件夹，可使用 `setting set message.folder <路径>` 改为其他目录，按 <kbd>Tab</kbd> 可直接补全目录路径。
2. 当监听目录中的 `.md` 文件新建或修改时，提示符前会出现红色 `[M]`，表示仍有未读内容。
3. 使用 `message list` 查看所有未读文件，`message last` 查看最近修改的文件内容，或通过 `message detail <文件名>` 定位并阅读指定文件。

若要取消监听，可运行 `setting set message.folder ""` 将路径清空。

## 提示符名称与主题

- `setting set prompt.name <名称>`：调整提示符名称，留空则恢复默认的 `mycli`。
- `setting set prompt.theme <blue|blue-purple|red-yellow|purple-orange>`：在纯蓝与多种渐变主题之间切换。
- `setting set prompt.input_ellipsis.enabled <true|false>`：开启后，当输入或自动补全内容超过指定长度时，会围绕光标保留左右两侧的可视窗口，并用 `.` 填充被截断的区域，避免光标被推到屏幕之外。
- `setting set prompt.input_ellipsis.left_width <列宽>` / `setting set prompt.input_ellipsis.right_width <列宽>`：分别配置光标左侧可保留的最大宽度与整体可视窗口的最大显示宽度（单位为等宽字符数），默认值分别为 `30` 与 `50`，必须为非负整数。
- `setting set prompt.theme_art_path.<theme> <path>`：为指定主题配置图片结构化文本路径（例如 `prompt.theme_art_path.red-yellow`），仅接受 `.climg` 文件并支持路径补全，搭配 `tools/image_to_art.py` 生成即可在 `show MyCLI` 中显示彩色图片。
- `setting set prompt.theme_art_path <path>`：兼容旧配置的别名，等价于设置 `prompt.theme_art_path.blue-purple`，同样要求 `.climg` 后缀。

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

`optionPaths` 和 `positionalPaths` 支持声明路径类型与后缀过滤：

```ini
[image-art]
summary=Convert images into MyCLI art
type=python
exec=python3
script=./tools/image_to_art.py
positional=<input> <output>
positionalPaths=1:file,2:file:.climg
```

上述配置会将第一个位置参数视为源文件，第二个位置参数限制为 `.climg` 输出文件。补全、帮助文本与错误提示会自动同步这一要求。
同理，`optionPaths=--output:file:.climg|.png` 可用于限制选项值的后缀范围。

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
- `tools.hpp`：路径补全实现、核心内置命令（`show`/`setting`/`exit`）以及所有工具的统一注册入口。
- `tools/tool_common.hpp`：为各个工具头文件提供共享的执行辅助函数与类型。
- `tools/*.hpp`：每个内置命令对应一个独立头文件（例如 `tools/ls.hpp`、`tools/cat.hpp`、`tools/mv.hpp`），包含 UI 定义、执行逻辑以及 `tool::make_*_tool()` 工厂函数。
- `tools/pytool.py`：示例 Python 工具脚本。
- `tools/image_to_art.py`：将图片转为 `.climg` 结构化文本的脚本，可配合动态工具 `image-art` 使用。
- `settings/`：默认配置目录，包含 `mycli_settings.conf`、`mycli_tools.conf`、`mycli_llm_history.json`。

## 开发提示

- 所有可执行文件均需使用 C++17 编译。
- 若新增命令，记得在 `register_all_tools()` 中注册并为选项或位置参数配置合适的路径类型/后缀（使用 `tool::positional(...)` 和 `OptionSpec::allowedExtensions`）。
- 自定义补全策略时，可复用 `pathCandidatesForWord` 与 `analyzePositionalPathContext` 等辅助函数，必要时传入扩展名列表与目录控制参数。
- 按照“一命令一文件”的约定扩展内置工具：在 `tools/` 目录下创建 `<name>.hpp`，实现 `ToolSpec`/执行逻辑/可选补全工厂，随后在 `tools.hpp` 中 `#include` 新文件并调用 `REG.registerTool(tool::make_<name>_tool())`；完成后同步更新本文档中相关说明。
