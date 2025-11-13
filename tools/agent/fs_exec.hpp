#pragma once

#include "../tool_common.hpp"
#include "../../utils/agent_commands.hpp"

namespace tool {

inline ToolSpec build_exec_spec(const std::string& name,
                                const std::string& summaryEn,
                                const std::string& summaryZh,
                                const std::string& helpEn,
                                const std::string& helpZh,
                                std::vector<OptionSpec> options = {}){
  ToolSpec spec;
  spec.name = name;
  spec.summary = summaryEn;
  set_tool_summary_locale(spec, "en", summaryEn);
  set_tool_summary_locale(spec, "zh", summaryZh);
  spec.help = helpEn;
  set_tool_help_locale(spec, "en", helpEn);
  set_tool_help_locale(spec, "zh", helpZh);
  spec.options = std::move(options);
  spec.hidden = true;
  spec.requiresExplicitExpose = true;
  return spec;
}

inline SubcommandSpec exec_subcommand_from(const ToolSpec& spec){
  SubcommandSpec sub;
  auto pos = spec.name.find_last_of('.');
  sub.name = (pos == std::string::npos) ? spec.name : spec.name.substr(pos + 1);
  sub.options = spec.options;
  sub.positional = spec.positional;
  sub.mutexGroups = {};
  sub.handler = nullptr;
  return sub;
}

struct FsExecShell {
  static ToolSpec ui(){
    return build_exec_spec(
      "fs.exec.shell",
      "Execute a shell command with guard integration",
      "执行带守卫的 shell 命令",
      "fs.exec.shell --command <text>",
      "fs.exec.shell --command <命令>",
      {
        OptionSpec{"--command", true, {}, nullptr, true, "<command>"}
      }
    );
  }

  static ToolExecutionResult run(const ToolExecutionRequest& request){
    return agent::command_exec_shell(request);
  }
};

struct FsExecPython {
  static ToolSpec ui(){
    return build_exec_spec(
      "fs.exec.python",
      "Run Python code in the sandbox",
      "在沙盒中执行 Python 代码",
      "fs.exec.python (--script <path> | --code <text>)",
      "fs.exec.python (--script <路径> | --code <代码>)",
      {
        OptionSpec{"--script", true, {}, nullptr, false, "<path>"},
        OptionSpec{"--code", true, {}, nullptr, false, "<code>"}
      }
    );
  }

  static ToolExecutionResult run(const ToolExecutionRequest& request){
    return agent::command_exec_python(request);
  }
};

struct FsExec {
  static ToolSpec ui(){
    ToolSpec spec;
    spec.name = "fs.exec";
    spec.summary = "Run guarded execution helpers";
    set_tool_summary_locale(spec, "en", "Run guarded execution helpers");
    set_tool_summary_locale(spec, "zh", "运行受守卫保护的执行工具");
    spec.help = "fs.exec <shell|python> ...";
    set_tool_help_locale(spec, "en", spec.help);
    set_tool_help_locale(spec, "zh", "fs.exec <shell|python> ...");
    spec.hidden = true;
    spec.requiresExplicitExpose = true;
    spec.subs = {
      exec_subcommand_from(FsExecShell::ui()),
      exec_subcommand_from(FsExecPython::ui())
    };
    return spec;
  }

  static ToolExecutionResult run(const ToolExecutionRequest& request){
    if(request.tokens.size() < 2){
      g_parse_error_cmd = "fs.exec";
      return detail::text_result("usage: fs.exec <subcommand> ...\n", 1);
    }
    const std::string& sub = request.tokens[1];
    ToolExecutionRequest forwarded = request;
    forwarded.tokens.clear();
    forwarded.tokens.push_back("fs.exec." + sub);
    for(size_t i = 2; i < request.tokens.size(); ++i){
      forwarded.tokens.push_back(request.tokens[i]);
    }
    if(sub == "shell") return FsExecShell::run(forwarded);
    if(sub == "python") return FsExecPython::run(forwarded);
    g_parse_error_cmd = "fs.exec";
    return detail::text_result("unknown fs.exec subcommand: " + sub + "\n", 1);
  }
};

inline ToolDefinition make_fs_exec_tool(){
  ToolDefinition def;
  def.ui = FsExec::ui();
  def.executor = FsExec::run;
  return def;
}

} // namespace tool

