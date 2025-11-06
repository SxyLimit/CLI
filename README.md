# mycli

一个演示用的交互式 CLI，展示了命令补全、路径类型约束和状态提示等功能。

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

## 目录结构

- `main.cpp`：REPL 主循环、渲染及补全逻辑。
- `globals.hpp`：公共类型、辅助函数与全局状态声明。
- `tools.hpp`：路径补全实现、命令/状态注册、动态工具加载。
- `tools/pytool.py`：示例 Python 工具脚本。
- `mycli_tools.conf`：外部工具配置示例。
- `tools/todo.hpp` / `tools/todo_impl.hpp`：`ToDoListEditor` 命令的全部实现，包含任务管理、存储与补全逻辑。

## ToDoListEditor 使用指南

`ToDoListEditor` 是一个带有自动补全和即时校验的任务管理命令。所有子命令、选项、可选值都会在输入时给出提示，对于必须满足预设枚举的字段会在验证失败时以红色错误信息提醒。

### 初始化与存储

1. **默认目录与设置**：首次启动时会自动在当前工作目录下建立待办存档。如果需要切换保存位置，可执行 `setting set todo.storage_dir <dir>`（值为目录类型，补全同样提供子目录列表）或 `ToDoListEditor Setup <dir>`。命令会创建：
   - `name.tdle`：记录现有任务名称与数量。
   - `operation.tdle`：追踪全部操作历史，便于恢复。
   - `Details/xxx.tdle`：每个任务的详细数据文件。
2. **配置文件**：根目录会生成 `tdle_config.json` 并与设置保持同步，记录最近一次使用的存储目录。

### 创建任务

运行 `ToDoListEditor Creat` 后直接回车，即可进入逐项引导式的创建向导：

1. **Name**（必填）——自动校验合法字符，并检查是否与现有任务重名。
2. **ToDo**（必填）——至少填写一条待办内容，支持使用 `;` 分隔多条记录。
3. **StartTime / Deadline**（可选）——默认光标会填入当前时间，直接回车即可沿用；在字段内输入任意数字后按 `Tab` 会自动补全 `yyyy.mm.dd aa:bb:cc` 格式所需的分隔符。补全同样支持相对时间（如 `+2d`、`+4h`）以及周期表达式（`per d`、`per 2w` 等），格式错误会立刻以红色提示。
4. **Tag / Urgency / Progress / Per / Template / Subtask / Pre / Post**（可选）——所有字段都具备和主命令一致的自动补全：
   - 标签支持大小写不敏感、子序列匹配，可一次输入多个标签（用空格或逗号分隔）。
   - 紧急程度提供 `none/low/normal/high/critical` 五档候选。
   - 周期、模板、关联任务字段的候选列表会实时读取现有数据。
   - 子任务名称同样支持批量录入。

向导中的每一步都可以直接回车跳过（保留默认值或留空），补全行为遵循全局的忽略大小写与子序列匹配设定。当需要使用旧式的一行命令时，也可以继续输入 `ToDoListEditor Creat <name> Add "..." StartTime ...` 等参数，行为与向导一致。

### 更新任务

- `ToDoListEditor Update <name> Add "ToDo"`：追加新的待办内容，并记录至任务历史。
- `ToDoListEditor Update <name> Reset StartTime/Deadline`：重设起止时间，规则同创建命令。
- `ToDoListEditor Update <name> Tag ...`：增加或移除分类标签。
- `ToDoListEditor Update <name> Urgency ...`：修改紧急程度，所有等级自动补全。
- `ToDoListEditor Update <name> Progress ...`：以百分比或步骤更新进度。
- `ToDoListEditor Update <name> Subtask ...`：新增、调整或删除子任务，以及更新子任务进度。
- `ToDoListEditor Update <name> Depends/Unlocks ...`：设置前驱后继任务关系。
- `ToDoListEditor Update <name> Template ...`：应用或移除任务模板，模板名称同样会补全。

### 删除任务

- `ToDoListEditor Delete <name>`：删除指定任务；若任务为周期类型，会提示是否仅删除当前周期实例。
- `ToDoListEditor Delete <name> per`：直接删除整个周期任务。
- 所有删除操作均需 `y/n` 二次确认，输入时同样有自动补全。

### 查询命令

- `ToDoListEditor Query`：列出所有未过期任务，按 deadline 升序排列。
- `ToDoListEditor Query +<time>`：如 `+3d`，查询距离当前时间指定范围内的任务。
- `ToDoListEditor Today`：查看今日正在执行的任务。
- `ToDoListEditor Today Deadline`：仅查看今日截止的任务。
- `ToDoListEditor QueryDetail <name>`：展示任务详情，包括历史记录、子任务、标签与依赖关系。
- `ToDoListEditor QueryLast <name>`：查看最近一次更新内容。
- `ToDoListEditor Finished`：列出所有已完成任务，并可选择是否批量清理。

### 标签、紧急提醒与状态栏

- 每个任务可设置多个分类标签，通过 `ToDoListEditor Query --tag <tag>`（自动补全）快速筛选。
- 紧急程度支持 `none/low/normal/high/critical` 五档。处于最高等级的任务会在 CLI 启动时自动提醒，并在状态栏显示摘要。
- 进度可用百分比或步骤方式记录，支持在详情中查看。

### 模板与子任务

- `ToDoListEditor Template Create <name>`：创建任务模板，模板字段与任务相同。
- `ToDoListEditor Template Apply <task> <template>`：为任务应用模板；`Template Remove` 可移除。
- 子任务与依赖关系会同步保存到 `Details` 目录中对应的 `.tdle` 文件。

### 历史追踪与恢复

- 所有操作都会写入 `operation.tdle`，包括时间戳和具体指令。
- 当误删或需要恢复任务时，可通过历史记录回溯操作并手动还原。

## 开发提示

- 所有可执行文件均需使用 C++17 编译。
- 若新增命令，记得在 `register_all_tools()` 中注册并为选项配置合适的路径类型。
- 自定义补全策略时，可复用 `pathCandidatesForWord` 与 `analyzePositionalPathContext` 等辅助函数。
