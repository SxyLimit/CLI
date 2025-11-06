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

1. **首次运行**：执行 `ToDoListEditor Setup <dir>` 选择一个目录作为存储位置。命令会创建：
   - `name.tdle`：记录现有任务名称与数量。
   - `operation.tdle`：追踪全部操作历史，便于恢复。
   - `Details/xxx.tdle`：每个任务的详细数据文件。
2. **配置文件**：根目录会生成 `tdle_config.json`，记录最后一次使用的存储目录。

### 创建任务

```text
ToDoListEditor Create <name> "ToDo"
```

- 任务名称仅能包含大小写字母、数字和下划线，若已存在会给出红色错误提示。
- 默认起止时间为当前时间，可通过补全选择：
  - 绝对时间 `yyyy.mm.dd aa:bb:cc`（支持只写日期或只写时间）。
  - 相对时间 `+10d`、`+30m`、`+90s`。
  - 周期任务 `per d`、`per 2w`、`per 3m`、`per y` 等。
- 可附加参数：分类标签（`--tag`）、紧急程度（`--urgency`，允许的值均提供补全）、进度（百分比或步骤）、模板、子任务、前驱/后继任务。

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
