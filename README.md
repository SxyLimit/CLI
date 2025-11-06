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

## 开发提示

- 所有可执行文件均需使用 C++17 编译。
- 若新增命令，记得在 `register_all_tools()` 中注册并为选项配置合适的路径类型。
- 自定义补全策略时，可复用 `pathCandidatesForWord` 与 `analyzePositionalPathContext` 等辅助函数。
