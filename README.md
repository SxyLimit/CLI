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

无论从哪个目录启动，CLI 都会始终使用自身根目录下的 `.env` 文件，确保配置来源固定且与仓库一起分发。若根目录不存在 `.env`，可手动复制示例或自行创建，该文件同样会用于保存 `HOME_PATH` 等设置项。

## 主要特性

- **命令注册与帮助**：通过 `ToolRegistry` 注册内置工具，可使用 `help` 查看命令说明。
- **路径补全与类型校验**：根据命令占位符或选项定义推断路径类型（文件/目录），补全时自动过滤；支持为文件参数声明允许的后缀（例如 `.climg`），同时在帮助文档与错误提示中给出明确指引。
- **命令委托**：`run <command> [args...]` 会在当前 shell 中执行任意系统命令，参数使用 `shellEscape` 逐项转义。通过共享的执行助手，命令启动前会暂时恢复终端默认光标/渲染状态，结束后再切回 REPL，CLI 里任何触发外部程序的场景（包括配置文件注册的动态工具）都遵循同样的流程，避免指针渲染异常。
- **候选提示与 Ghost 文本**：在输入行下方展示至多三个候选项，输入末尾补全不存在时给出上下文提示。
- **历史命令补全**：输入 `p` 后加空格即可调出最近使用的命令列表，按 Tab 将选中的历史指令直接填入输入行，也可以单独执行 `p` 查看带编号的历史记录。
- **状态栏扩展**：可通过 `StatusProvider` 注册自定义状态（示例中显示当前工作目录）。
- **外部工具配置**：支持在配置目录（默认 `./settings/`）下的 `mycli_tools.conf` 中用 INI 语法新增命令及子命令，含互斥选项、动态执行等；所有配置驱动的命令在执行前也会自动恢复终端状态，再在收尾阶段切回 REPL。
- **消息提醒**：可监听指定目录（默认当前目录下的 `message/`）中的 `.md` 文件，新建或修改后提示符前会显示红色 `[M]`，通过 `message list/last/detail` 查看。
- **可定制提示符**：通过设置 `prompt.name` 与 `prompt.theme` 自定义提示符名称及颜色。提供纯蓝、蓝紫、红黄渐变与紫橙渐变四种主题，并可为任意主题配置结构化图片（`prompt.theme_art_path.<theme>`）以在 `show MyCLI` 中输出彩色图案。
- **LLM 接口**：提供 `llm` 命令，调用 `tools/llm.py` 通过 Moonshot(Kimi) 接口（或本地回显模式）完成调用与历史查看。

## 内置工具速查

| 命令 | 基本用法 | 说明 |
| --- | --- | --- |
| `help` | `help`<br>`help <command>` | 查看可用命令与参数说明，支持针对单个命令输出详细帮助。 |
| `show` | `show LICENSE`<br>`show MyCLI` | 查看随项目附带的许可证与 MyCLI 信息。 |
| `clear` | `clear` | 清空屏幕并将光标重置到左上角。 |
| `p` | `p`<br>`p` 后接空格再按 <kbd>Tab</kbd> | 列出最近输入的命令；在 `p` 后加空格触发历史补全，按 <kbd>Tab</kbd> 将选中的指令直接填入输入框。 |
| `setting` | `setting get [分段…]`<br>`setting set <完整键> <值>` | 读取或修改配置项。`get` 可按层级浏览配置树，`set` 需先补全到具体键后再输入新值，自动给出布尔/枚举/路径提示。详见下文“设置命令”。 |
| `run` | `run <command> [args…]` | 逐项转义后执行任意系统命令，执行期间会暂时恢复终端默认状态以避免交互异常；同样适用于配置文件新增的外部命令。 |
| `llm` | `llm call <消息…>`<br>`llm recall` 等 | 通过 Python 助手异步调用 Moonshot/Kimi 接口并管理历史会话。 |
| `message` | `message list`<br>`message last`<br>`message detail <文件>` | 监听 Markdown 通知目录，列出未读文件、查看最近修改的文件，或按文件名读取具体内容。 |
| `memory` | `memory import/list/show/search/stats/note/query/monitor …` | 导入个人/知识文档，浏览摘要、监控异步导入，或基于记忆回答问题。 |
| `cd` | `cd <路径>`<br>`cd -o [-a\|-c]` | 切换工作目录；搭配 `-o` 可修改提示符显示模式（`-a` 仅显示目录名，`-c` 恢复完整路径）。 |
| `ls` | `ls [-a] [-l] [目录]` | 简化版目录列表，支持展示隐藏文件与长列表模式。 |
| `cat` | `cat <path> [选项]` | 便于人工快速查看文件内容；行为与 Agent 使用的 `fs.read` 保持一致。 |
| `mv` | `mv <source> <target>` | 移动或重命名文件/目录。 |
| `touch` | `touch <path> [更多路径]` | 同 Linux `touch`，不存在则创建文件，存在时更新修改时间（可作用于目录）。 |
| `mkdir` | `mkdir [--parents|-p] <path> [更多路径]` | 同 Linux `mkdir`，创建目录，`-p` 可自动创建父目录并允许目标已存在。 |
| `rm` | `rm [-r] <path> [更多路径]` | 删除文件，带 `-r` 可递归删除目录。 |
| `exit` / `quit` | `exit` 或 `quit` | 结束 REPL 会话。 |

> Agent 专用的 `fs.*` 工具已在下文“Agent 命令与协作流程”章节集中说明。

## 配置目录

- 默认情况下，所有配置文件存放在 `./settings/` 目录中，包括 `mycli_settings.conf`、`mycli_tools.conf` 与 `mycli_llm_history.json`。
- `mycli_settings.conf` 采用逐行 `key=value` 的纯文本格式，方便手动编辑；不要误认为是 JSON 文件。
- 运行时可通过 `setting set home.path <目录>` 修改配置目录，CLI 会自动迁移已有文件并更新监听路径。
- 也可以在 `.env` 或系统环境变量中设置 `HOME_PATH=<目录>`，用于在启动前指定配置目录位置。

## 设置命令

`setting` 命令的语法固定为 `setting <get|set> …`：

- `setting get [分段…]`：
  - 不带额外参数时会列出所有可用的设置项；
  - 传入某个前缀则仅展开该前缀下的子键，便于逐层浏览树状结构；
  - 当补全到叶子节点时会直接显示当前值。
- `setting set <完整键> <值>`：在补全到完整键之后再录入新值。命令会根据键类型提供布尔、枚举或路径的候选项，路径类键支持在输入 `/`、`./` 后继续按 <kbd>Tab</kbd> 完成文件/目录名，并自动校验允许的后缀。

键名与可选值均支持多次补全：在任意层级按 <kbd>Tab</kbd> 可以查看下一层的候选节点，确保不会误输入不存在的键。

### 可配置项

| 键名 | 类型 / 可选值 | 默认值 | 说明 |
| --- | --- | --- | --- |
| `home.path` | 目录路径 | `./settings` | 配置目录位置。更新后会自动迁移 `mycli_settings.conf`、`mycli_tools.conf`、`mycli_llm_history.json`，并写入 `.env` 的 `HOME_PATH`。 |
| `prompt.cwd` | `full` / `omit` / `hidden` | `full` | 控制状态栏中是否显示当前工作目录。也可通过 `cd -o` 快捷修改。 |
| `completion.ignore_case` | `true` / `false` | `false` | 是否在补全时忽略大小写。 |
| `completion.subsequence` | `true` / `false` | `false` | 是否启用子序列匹配。 |
| `completion.subsequence_mode` | `ranked` / `greedy` | `ranked` | 子序列匹配策略；`ranked` 会基于得分排序候选项。 |
| `language` | 语言代码 | `en` | 控制帮助与提示语言，默认内置 `en`、`zh`，也可以输入自定义语言并用于动态工具。 |
| `ui.path_error_hint` | `true` / `false` | `true` | 执行命令时是否在错误提示中补充路径诊断信息。 |
| `message.folder` | 目录路径 | `./message` | Markdown 消息监听目录，留空可停用监听。 |
| `prompt.name` | 字符串 | `mycli` | 提示符名称。 |
| `prompt.theme` | `blue` / `blue-purple` / `red-yellow` / `purple-orange` | `blue` | 提示符配色主题。 |
| `prompt.theme_art_path.<theme>` | `.climg` 文件路径 | 空 | 为指定主题配置结构化彩色图案；`prompt.theme_art_path` 仍保留为 `prompt.theme_art_path.blue-purple` 的别名。 |
| `prompt.input_ellipsis.enabled` | `true` / `false` | `false` | 是否在输入过长时启用“省略号视窗”。 |
| `prompt.input_ellipsis.left_width` | 非负整数 | `30` | 视窗左侧最多保留的列数，仅在启用省略号时生效。 |
| `prompt.input_ellipsis.right_width` | 非负整数或 `default` | `default`（实时取终端宽度减去状态栏与提示符宽度） | 整体视窗的最大宽度，仅在启用省略号时生效。 |
| `history.recent_limit` | 非负整数 | `10` | 历史记录最多保留的条目数。 |
| `agent.fs_tools.expose` | `true` / `false` | `false` | 是否在 CLI 中暴露 `fs.read` / `fs.write` / `fs.create` / `fs.tree` 命令及其补全。 |
| `memory.enabled` | `true` / `false` | `true` | 是否启用 Memory 系统。 |
| `memory.root` | 目录路径 | `${home.path}/memory` | Memory 根目录。 |
| `memory.index_file` | 文件路径 | `${memory.root}/memory_index.jsonl` | 目录/文件摘要索引。 |
| `memory.personal_subdir` | 字符串 | `personal` | 个人记忆子目录名称。 |
| `memory.summary.lang` | 语言代码 | 同 `language` | 摘要生成语言。 |
| `memory.summary.min_len` | 非负整数 | `50` | 摘要最短长度（字符）。 |
| `memory.summary.max_len` | 非负整数 | `100` | 摘要最长长度（字符）。 |
| `memory.max_bootstrap_depth` | 非负整数 | `1` | Agent 启动时默认暴露的最大层级。 |

当设置值来自文件路径时，CLI 会根据键定义的路径类型（文件或目录）与允许后缀自动筛选候选项，避免误选不受支持的文件。

## 消息提醒与查看

1. 默认监听当前目录下的 `message/` 文件夹，可使用 `setting set message.folder <路径>` 改为其他目录，按 <kbd>Tab</kbd> 可直接补全目录路径。
2. 当监听目录中的 `.md` 文件新建或修改时，提示符前会出现红色 `[M]`，表示仍有未读内容。
3. 使用 `message list` 查看所有未读文件，`message last` 查看最近修改的文件内容，或通过 `message detail <文件名>` 定位并阅读指定文件。

若要取消监听，可运行 `setting set message.folder ""` 将路径清空。

## 提示符名称与主题

- `setting set prompt.name <名称>`：调整提示符名称，留空则恢复默认的 `mycli`。
- `setting set prompt.theme <blue|blue-purple|red-yellow|purple-orange>`：在纯蓝与多种渐变主题之间切换。
- `setting set prompt.input_ellipsis.enabled <true|false>`：开启后，当输入或自动补全内容超过指定长度时，会围绕光标保留左右两侧的可视窗口，并用 `.` 填充被截断的区域，避免光标被推到屏幕之外。
- `setting set prompt.input_ellipsis.left_width <列宽>` / `setting set prompt.input_ellipsis.right_width <列宽|default>`：分别配置光标左侧可保留的最大宽度与整体可视窗口的最大显示宽度（单位为等宽字符数）。左侧默认 `30`，右侧默认跟随终端宽度减去提示符/状态栏占用列数，可通过 `default` 关键字恢复自适应模式，或填入非负整数锁定固定宽度。
- `setting set history.recent_limit <数量>`：调整历史指令最多保留的条目数（默认 10，设为 0 可禁用历史记录）。
- `setting set agent.fs_tools.expose <true|false>`：是否在 CLI 中暴露 `fs.read` / `fs.write` / `fs.create` / `fs.tree` 命令。默认 `false`（仅 Agent 调用），设为 `true` 后可手动运行并恢复补全/帮助。
- `setting set prompt.theme_art_path.<theme> <path>`：为指定主题配置图片结构化文本路径（例如 `prompt.theme_art_path.red-yellow`），仅接受 `.climg` 文件并在补全时只展示目录与 `.climg` 文件；搭配 `tools/image_to_art.py` 生成即可在 `show MyCLI` 中显示彩色图片（旧版本的 `prompt.theme_art_path` 仍作为 `prompt.theme_art_path.blue-purple` 的别名保留）。
- Memory 设置：可使用 `setting set memory.root <目录>`、`memory.index_file`、`memory.personal_subdir`、`memory.summary.lang` 等键管理记忆系统的目录与摘要参数。

## Memory 命令使用说明

`memory` 命令围绕 `${home.path}/memory` 目录工作，默认包含固定的 `personal/` 子目录以存放个人档案、偏好与对话记录，也会为常规知识自动维护 `knowledge/` 根目录。所有由工具创建的目录/文件名会自动清洗为仅包含英文字母、数字、`-`、`_` 的安全格式，避免因 UTF-8 特殊字符导致解析异常；导入 `.md/.txt` 时还会按标题与长度切分为统一颗粒度的片段，避免超长文档影响检索与摘要质量。

导入命令的源路径补全会限制为 `.md`、`.txt` 文件或目录，并保持 ASCII 安全的命名规则。

- `memory import <src>`：将 `.md/.txt` 文件或目录导入到 `personal/` 或 `knowledge/<category>/` 下，导入会以异步方式执行，提示符前显示黄色 `[I]`（进行中）与红色 `[I]`（完成），自动重建摘要索引、对路径逐段做安全命名，并将长文档按标题构建层级文件夹（文件名来自各级标题而非 `-pX` 后缀），在同一文件树内生成含义清晰的分节文件以统一颗粒度。
- `memory list [path]`：按目录层级浏览记忆摘要，默认展示根目录下的一级分类和直接文件。
- `memory show <path>`：查看单个节点的元数据和摘要，可通过 `--content` 读取正文。
- `memory search <keywords...>`：在摘要或正文中进行关键词检索，支持 `--scope personal|knowledge`。
- `memory note <text>`：在 `personal/notes/` 下快速追加一条个人 note 并刷新摘要索引。
- `memory query <question>`：仅基于记忆内容生成回答，执行期间提示符前会显示黄色 `[Q]`，结束后变为红色。
- `memory monitor`：实时查看异步导入与其他记忆事件的 JSONL 日志（含模型摘要的 system/user prompt 与返回文本），按 `q` 退出监控。所有 Memory 相关的 LLM 调用也会写入 `${memory.root}/memory_llm_calls.jsonl` 便于排查。

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

## Agent 命令与协作流程

### Agent 命令速查

| 命令 | 基本用法 | 说明 |
| --- | --- | --- |
| `agent run` | `agent run <goal…>` | 启动内置 Python Agent 并在后台持续协作。 |
| `agent saferun` | `agent saferun [-a] <todo…>` | 守卫模式运行 Agent，默认仅在调用非 `fs.*` 工具或 `fs.exec.shell` 时请求人工审核，加上 `-a` 则所有工具执行前都需审核。 |
| `agent monitor` | `agent monitor [session_id]` | 监控最新或指定会话的执行轨迹，监控界面按 `q` 退出。 |
| `agent tools` | `agent tools --json` | 导出沙盒工具的 JSON Schema，便于外部 Agent 校验契约。 |

`agent run` 会调用 `tools/agent/agent.py`（默认通过 `python3`），采用行分隔 JSON 协议创建会话：CLI 先发送 `hello`（工具目录、配额与沙盒策略）和 `start`（目标描述与工作目录），Python Agent 可多次请求工具调用，返回 `final` 后 CLI 完成收尾。所有消息和工具调用会写入 `./artifacts/<session_id>/transcript.jsonl` 与 `summary.txt`，并在 `final` 携带 `artifacts[]` 时同步落盘。

`agent saferun` 复用了同一协作协议，但会在触发关键操作时暂停等待人工确认：默认情况下所有非 `fs.*` 工具以及 `fs.exec.shell` 都会先进入人工审核；添加 `-a` 后则进一步要求每一次工具调用都需被审核通过才会执行。审核过程会在 `agent monitor` 中展示并支持 `y/n` 快速批准或拒绝。

执行期间提示符前会亮起黄色 `[A]`，会话结束后变为红色提醒查看 `summary.txt` 或进入监控；若守卫等待人工确认（如 `agent saferun` 触发的人工审核），`[A]` 会以黄色字体配合灰色括号闪烁提示尽快运行 `agent monitor`。监控界面支持 Tab 补全会话 ID，并根据不同的 Agent 指令（如 `fs.read`/`fs.write`/`fs.exec.shell`）以不同颜色渲染轨迹，同时将守卫告警以红色高亮 `y/n` 交互，退出后指示器会自动熄灭。

### 沙盒工具速查

所有 `fs.*` 工具默认仅供 Agent 调用，不会出现在 CLI 的补全/帮助列表；如需手动运行，可执行 `setting set agent.fs_tools.expose true` 暂时开放。下表按照统一格式列出可用工具及其关键能力：

| 工具 | 示例调用 | 主要用途与要点 |
| --- | --- | --- |
| `fs.read` | `fs.read <path> --max-bytes 4096 --with-line-numbers` | 读取受限白名单内的文本文件，支持按字节/行采样、区间读取与哈希校验。 |
| `fs.write` | `fs.write <path> --mode overwrite --content "..." --atomic` | 以覆盖或追加方式写入文本，可选行尾转换、备份与原子落盘，返回写前/写后哈希。 |
| `fs.create` | `fs.create <path> --content-file seed.txt --create-parents` | 在目标不存在时创建文件，支持一次性写入、父目录创建、原子写入与试运行。 |
| `fs.tree` | `fs.tree <root> --depth 3 --format json --ext .cpp` | 生成目录快照并支持深度、后缀、忽略规则筛选，返回节点统计与截断信息。 |
| `fs.todo plan` | `fs.todo plan --title "Refactor"` | 创建带版本号的任务计划，自动记录里程碑信息。 |
| `fs.todo view` | `fs.todo view --active` | 查看当前计划详情，可聚焦进行中或全部步骤。 |
| `fs.todo add` | `fs.todo add <parent> --title "Implement"` | 在计划中新增步骤，支持指定父节点与排序位置。 |
| `fs.todo update` | `fs.todo update <step> --title "Review"` | 修改已有步骤的标题、负责人等元数据。 |
| `fs.todo remove` | `fs.todo remove <step>` | 删除步骤并自动清理依赖引用。 |
| `fs.todo reorder` | `fs.todo reorder <step> --before other` | 调整步骤顺序，保持拓扑关系正确。 |
| `fs.todo dep.set` | `fs.todo dep.set <step> --depends a,b` | 重建步骤依赖列表，确保图结构与实际流程同步。 |
| `fs.todo dep.add` | `fs.todo dep.add <step> --depends blocker` | 为指定步骤追加依赖关系。 |
| `fs.todo dep.remove` | `fs.todo dep.remove <step> --depends blocker` | 从依赖图中移除指定边。 |
| `fs.todo split` | `fs.todo split <step> --into a,b` | 将大型步骤拆分为多个子项并保留上下文。 |
| `fs.todo merge` | `fs.todo merge <stepA> <stepB>` | 合并步骤并合并依赖/注释。 |
| `fs.todo mark` | `fs.todo mark <step> --status done` | 推进状态机（如进行中、已完成），生成对应时间戳。 |
| `fs.todo block` | `fs.todo block <step> --reason "Waiting"` | 标记阻塞原因并触发风险评估。 |
| `fs.todo unblock` | `fs.todo unblock <step>` | 解除阻塞并恢复正常流程。 |
| `fs.todo checklist` | `fs.todo checklist <step> --item "Review" --done true` | 维护步骤内的检查清单。 |
| `fs.todo annotate` | `fs.todo annotate <step> --note "Need tests"` | 为步骤追加注释或补充材料引用。 |
| `fs.todo snapshot` | `fs.todo snapshot --label nightly` | 保存计划快照以便后续比对。 |
| `fs.todo history` | `fs.todo history --limit 20` | 查看计划变更历史。 |
| `fs.todo undo` | `fs.todo undo` | 撤销上一条计划操作。 |
| `fs.todo redo` | `fs.todo redo` | 重做最近一次撤销。 |
| `fs.todo brief` | `fs.todo brief --mode mic` | 基于当前计划生成 MIC 摘要。 |
| `fs.todo signal` | `fs.todo signal --type milestone --note "MVP"` | 记录编排信号以供指标回放。 |
| `fs.ctx scope` | `fs.ctx scope --show` | 查看或调整当前上下文作用域。 |
| `fs.ctx capture` | `fs.ctx capture <step>` | 捕获 side context 并关联到指定步骤。 |
| `fs.ctx pin` | `fs.ctx pin <step>` | 固定关键上下文片段，防止被自动清理。 |
| `fs.ctx unpin` | `fs.ctx unpin <step>` | 解除固定，允许上下文被回收。 |
| `fs.ctx pack_for_mic` | `fs.ctx pack_for_mic --steps a,b` | 将上下文打包成 MIC 摘要输入。 |
| `fs.ctx inject_todo` | `fs.ctx inject_todo <step>` | 将上下文绑定回计划节点，实现状态对齐。 |
| `fs.guard fs` | `fs.guard fs --path ./dangerous` | 审核文件系统操作的安全性，必要时请求人工确认。 |
| `fs.guard shell` | `fs.guard shell --command "rm -rf"` | 对潜在危险 shell 命令执行守卫评估。 |
| `fs.guard net` | `fs.guard net --url https://...` | 检查网络访问是否符合策略。 |
| `fs.exec shell` | `fs.exec shell --command "pytest"` | 在守卫通过后执行 shell 命令并捕获 stdout/stderr。 |
| `fs.exec python` | `fs.exec python --script script.py` | 执行 Python 代码片段或脚本，返回结构化结果。 |
| `fs.fs read` | `fs.fs read <path>` | 兼容旧版沙盒读取能力，并与新版工具共享配额。 |
| `fs.fs write_safe` | `fs.fs write_safe <path> --content "..."` | 受守卫保护的安全写入入口。 |
| `fs.fs snapshot` | `fs.fs snapshot --label before` | 生成当前目录快照。 |
| `fs.fs diff` | `fs.fs diff --from before --to after` | 对比两次快照差异。 |
| `fs.risk assess` | `fs.risk assess --step main` | 基于优先级与阻塞信息生成风险评估。 |
| `fs.request review` | `fs.request review --step main` | 汇总高风险步骤的人工审阅包。 |
| `fs.budget set` | `fs.budget set --tokens 5000 --minutes 20` | 设定 token/时间/请求数预算。 |
| `fs.budget meter` | `fs.budget meter --show` | 查看预算使用情况。 |
| `fs.timer` | `fs.timer --name delivery --minutes 30` | 注册超时提醒并在到期时提示。 |
| `fs.log event` | `fs.log event --type note --message "Tests added"` | 记录关键事件以供后续回放。 |
| `fs.report summary` | `fs.report summary --format markdown` | 汇总日志并生成报告或总结片段。 |

所有工具均返回结构化 JSON，方便外部编排器读取元信息、处理版本冲突并生成运行日志。`fs.ctx` 系列的扩展命令（如 `fs.ctx ingest`、`fs.ctx search`）在当前模式下会返回 `not_enabled` 占位结果，保留后续扩展空间。

沙盒文件相关工具共享统一的访问限制：仅允许读取/写入当前工作目录内的白名单文本后缀，执行前均会进行 `realpath` 校验；`cat` 命令等价于 `fs.read`，便于人工复核 Agent 的读取结果。

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
