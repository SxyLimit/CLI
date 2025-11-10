#pragma once

#include "tool_common.hpp"

namespace tool {

struct Mv {
  static ToolSpec ui(){
    ToolSpec spec;
    spec.name = "mv";
    spec.summary = "Move or rename files";
    set_tool_summary_locale(spec, "en", "Move or rename files");
    set_tool_summary_locale(spec, "zh", "移动或重命名文件");
    spec.positional = {"<source>", "<target>"};
    set_tool_help_locale(spec, "en", "mv <source> <target>");
    set_tool_help_locale(spec, "zh", "mv <源路径> <目标路径>");
    return spec;
  }

  static ToolExecutionResult run(const ToolExecutionRequest& request){
    const auto& args = request.tokens;
    if(args.size() != 3){
      g_parse_error_cmd = "mv";
      return detail::text_result("usage: mv <source> <target>\n", 1);
    }
    std::error_code ec;
    std::filesystem::rename(args[1], args[2], ec);
    if(ec){
      g_parse_error_cmd = "mv";
      return detail::text_result("mv: " + ec.message() + "\n", 1);
    }
    return detail::text_result("mv: success\n");
  }
};

inline ToolDefinition make_mv_tool(){
  ToolDefinition def;
  def.ui = Mv::ui();
  def.executor = Mv::run;
  return def;
}

} // namespace tool

