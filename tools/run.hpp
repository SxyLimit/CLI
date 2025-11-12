#pragma once

#include "tool_common.hpp"

namespace tool {

struct RunCommand {
  static ToolSpec ui(){
    ToolSpec spec;
    spec.name = "run";
    spec.summary = "Execute a system command";
    set_tool_summary_locale(spec, "en", "Execute a system command");
    set_tool_summary_locale(spec, "zh", "执行系统命令");
    set_tool_help_locale(spec, "en", "Usage: run <command> [args...]");
    set_tool_help_locale(spec, "zh", "用法：run <命令> [参数...]");
    spec.positional = {positional("<command>")};
    return spec;
  }

  static ToolExecutionResult run(const ToolExecutionRequest& request){
    const auto& args = request.tokens;
    if(args.size() < 2){
      g_parse_error_cmd = "run";
      return detail::text_result("usage: run <command> [args...]\n", 1);
    }
    std::string command;
    for(size_t i = 1; i < args.size(); ++i){
      if(i > 1) command.push_back(' ');
      command += shellEscape(args[i]);
    }
    auto result = detail::execute_shell(request, command);
    return result;
  }
};

inline ToolDefinition make_run_tool(){
  ToolDefinition def;
  def.ui = RunCommand::ui();
  def.executor = RunCommand::run;
  return def;
}

} // namespace tool

