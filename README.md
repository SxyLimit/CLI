# MyCLI

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
- **历史命令补全**：输入 `p` 后加空格即可调出最近使用的命令列表，按 Tab 将选中的历史指令直接填入输入行，也可以单独执行 `p` 查看带编号的历史记录。
- **状态栏扩展**：可通过 `StatusProvider` 注册自定义状态（示例中显示当前工作目录）。
- **外部工具配置**：支持在配置目录（默认 `./settings/`）下的 `mycli_tools.conf` 中用 INI 语法新增命令及子命令，含互斥选项、动态执行等。
- **消息提醒**：可监听指定目录（默认当前目录下的 `message/`）中的 `.md` 文件，新建或修改后提示符前会显示红色 `[M]`，通过 `message list/last/detail` 查看。
- **可定制提示符**：通过设置 `prompt.name` 与 `prompt.theme` 自定义提示符名称及颜色。提供纯蓝、蓝紫、红黄渐变与紫橙渐变四种主题，并可为任意主题配置结构化图片（`prompt.theme_art_path.<theme>`）以在 `show MyCLI` 中输出彩色图案。
- **LLM 接口**：提供 `llm` 命令，调用 `tools/llm.py` 通过 Moonshot(Kimi) 接口（或本地回显模式）完成调用与历史查看。

## 内置工具速查

| 命令 | 基本用法 | 说明 |
| --- | --- | --- |
| `show` | `show LICENSE`<br>`show MyCLI` | 查看随项目附带的许可证与 MyCLI 信息。 |
| `clear` | `clear` | 清空屏幕并将光标重置到左上角。 |
| `p` | `p`<br>`p` 后接空格再按 <kbd>Tab</kbd> | 列出最近输入的命令；在 `p` 后加空格触发历史补全，按 Tab 将选中的指令直接填入输入框。 |
| `setting` | `setting get [键前缀…]`<br>`setting set <完整键> <值>` | 读取或修改配置项，支持层级补全、布尔/枚举/路径提示等，详见下文“设置命令”。 |
| `run` | `run <command> [args…]` | 逐项转义后执行任意系统命令。 |
| `llm` | `llm call <消息…>`<br>`llm recall` | 通过 Python 助手异步调用 Moonshot/Kimi 接口并查看最近一次回复。 |
| `message` | `message list`<br>`message last`<br>`message detail <文件>` | 监听 Markdown 通知目录，列出未读文件、查看最近修改的文件，或按文件名读取具体内容。 |
| `cd` | `cd <路径> `<br>`cd -o [-a\|-c]` | 切换工作目录；搭配 `-o` 可修改提示符显示模式。 |
| `ls` | `ls [-a] [-l] [目录]` | 简化版目录列表，支持展示隐藏文件与长列表模式。 |
| `fs.read` | `fs.read <path> [--max-bytes N] [--head N|--tail N] [...]` | 在沙盒内读取文本文件，支持按范围/行号采样、计算哈希值、输出行号等。默认仅供 Agent 调用，如需手动使用可先执行 `setting set agent.fs_tools.expose true`。`cat` 命令现为该工具的别名。 |
| `fs.write` | `fs.write <path> (--content TEXT | --content-file FILE) [选项]` | 在沙盒内写入文本文件，支持覆盖/追加、换行符转换、原子化提交与备份，并提供写入前后的哈希摘要。同样默认仅对 Agent 开放，可通过 `agent.fs_tools.expose` 设置临时暴露给 CLI。 |
| `fs.create` | `fs.create <path> [--content TEXT | --content-file FILE] [选项]` | 新建沙盒内的文本文件，支持一次性写入初始内容、自动创建父目录或原子提交，便于生成 C/C++ 等白名单后缀的源文件。默认仅供 Agent 调用。 |
| `fs.tree` | `fs.tree <root> [--depth N] [--format json|text] [...]` | 构建指定目录的树形快照，可按深度、后缀及忽略规则过滤，并输出为 JSON 供 Agent 消费；默认隐藏于补全列表，需显式开启 `agent.fs_tools.expose` 才能直接调用。 |
| `agent` | `agent run <goal…>`<br>`agent tools --json`<br>`agent monitor [session_id]` | 启动内置 Python Agent，通过统一协议协作；`agent tools --json` 导出沙盒工具的 JSON Schema；`agent monitor` 实时查看最新或指定会话的操作轨迹，按 `q` 退出监控。 |
| `mv` | `mv <source> <target>` | 移动或重命名文件/目录。 |
| `rm` | `rm [-r] <path> [更多路径]` | 删除文件，带 `-r` 可递归删除目录。 |
| `exit` / `quit` | `exit` 或 `quit` | 结束 REPL 会话。 |

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
- `setting set history.recent_limit <数量>`：调整历史指令最多保留的条目数（默认 10，设为 0 可禁用历史记录）。
- `setting set agent.fs_tools.expose <true|false>`：是否在 CLI 中暴露 `fs.read` / `fs.write` / `fs.create` / `fs.tree` 命令。默认 `false`（仅 Agent 调用），设为 `true` 后可手动运行并恢复补全/帮助。
- `setting set prompt.theme_art_path.<theme> <path>`：为指定主题配置图片结构化文本路径（例如 `prompt.theme_art_path.red-yellow`），仅接受 `.climg` 文件并在补全时只展示目录与 `.climg` 文件；搭配 `tools/image_to_art.py` 生成即可在 `show MyCLI` 中显示彩色图片（旧版本的 `prompt.theme_art_path` 仍作为 `prompt.theme_art_path.blue-purple` 的别名保留）。

## LLM 命令使用说明

`llm` 命令由 `tools/llm.py` 实现，并默认持久化多轮会话（保存在 `./settings/mycli_llm_history.json`）：

- `llm call <消息>`：异步发送文本到模型并立即返回。当前会话的完整上下文（含系统提示词与历史消息）会一并交给模型，等待返回时提示符前会出现红色 `[L]` 提醒。
- `llm recall`：查看当前会话的完整对话历史，同时清除 `[L]` 提醒。
- `llm new`：创建一个全新的会话并自动切换过去，初始名称为 `未命名<N>-YYYYMMDDHHMMSS`。
- `llm switch <对话>`：切换到已有会话，支持补全会话名称。
- `llm rename <名称>`：为当前会话修改名称前缀，后缀中的时间戳保持创建时的值不变。

每个新会话在首次与模型对话后，会自动将历史消息再次发送给模型以生成一个不超过 10 个字的标题，实际存储格式为 `标题-YYYYMMDDHHMMSS`。

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

## 沙盒文件工具（`fs.read` / `fs.write` / `fs.create` / `fs.tree`）

为了支持 Agent 自动执行“读—改—验”的闭环，CLI 内置了四款具备沙盒保护的文件工具。它们只允许访问当前工作目录下、白名单后缀的文本资源（默认 `.py/.md/.txt/.json/.yaml/.toml/.html/.css/.js/.c/.cc/.cpp/.cxx/.h/.hh/.hpp`），所有路径在执行前都会 `realpath` 并验证是否位于沙盒根目录内。

- **`fs.read`**：读取文件内容，可通过 `--max-bytes` 控制最大读取量，使用 `--head`/`--tail` 按行采样，或借助 `--offset`/`--length` 指定字节区间。`--with-line-numbers` 会为输出加上行号，`--hash-only` 则仅返回哈希摘要。执行结果的元数据会记录读取范围、哈希值、是否截断以及耗时。
- **`fs.write`**：以最小副作用写入文本文件，支持 `--mode overwrite|append`、`--content`/`--content-file` 二选一输入、`--eol` 行尾转换、`--backup` 备份旧文件、`--atomic` 临时文件 + 重命名的原子落盘，并在元数据中给出写前/写后的哈希与写入字节数。若内容超出限制或编码不为 UTF-8，会返回统一错误码。
- **`fs.create`**：仅在目标文件不存在时创建新文件，可一次性写入 `--content` 文本或从 `--content-file` 复制内容，并支持 `--create-parents`、`--eol`、`--atomic`、`--dry-run` 等选项。元数据会记录写入字节数、哈希值、是否使用原子写入及耗时。
- **`fs.tree`**：生成目录快照，可通过 `--depth`、`--ext`、`--ignore-file`、`--include-hidden` 等选项筛选，并以 `--format json|text` 输出（Agent 推荐 JSON）。返回值会附带节点总数、是否截断与耗时信息。

`cat` 命令现在等价于 `fs.read`，便于人工快速复核 Agent 读取到的内容。

> 提示：四款沙盒工具默认仅供 Agent 调用，因而不会出现在补全/帮助列表中。若确实需要在 CLI 中直接运行，可执行 `setting set agent.fs_tools.expose true` 暂时暴露它们。

## Agent 协作流程

`agent run <goal…>` 会启动 `tools/agent/agent.py`（默认通过 `python3` 调用），并按照行分隔 JSON 协议与 Python 端建立会话。命令会在派发后台线程后立即返回，提示如何使用 `agent monitor` 追踪进度：

1. CLI 发送 `hello`（包含工具目录、调用限制与沙盒策略）与 `start`（目标描述、当前工作目录）。
2. Python Agent 可多次返回 `tool_call` 请求调用 `fs.read` / `fs.write` / `fs.create` / `fs.tree`，CLI 在执行前会写入 transcript 并按照限制截断 `stdout`。
3. Agent 返回 `final` 后会话结束；若进程退出但未显式 `final`，CLI 也会优雅收尾。

会话的完整轨迹会落在 `./artifacts/<session_id>/transcript.jsonl` 中，格式化记录每一次消息、工具调用的入参快照、执行耗时、截断信息及哈希摘要；`summary.txt` 会随流程更新当前结论或失败原因。若 `final` 消息内含 `artifacts[]`，CLI 会在同一目录写入对应文件并在 transcript 中登记路径。你也可以通过 `agent tools --json` 获取四款沙盒工具的 JSON Schema（含参数类型、互斥约束与路径元数据），便于外部 Agent 进行契约校验。

当 Agent 正在后台执行时，提示符前会亮起黄色 `[A]` 指示器；会话自然结束或失败后，它会变为红色 `[A]`，提示可以回顾 `summary.txt` 或进入监控模式。运行 `agent monitor [session_id]` 可实时跟踪最新或指定会话的 transcript（不带参数时使用最近一场会话），监控过程中按下 `q` 即可退出；退出监控后指示器会自动熄灭。`agent monitor` 的参数同样支持 Tab 自动补全，方便快速定位最近的会话 ID。

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
