#pragma once

#include "tool_common.hpp"
#include "fs_read.hpp"

namespace tool {

struct Cat {
  static ToolSpec ui(){
    ToolSpec spec = FsRead::ui();
    spec.name = "cat";
    spec.summary = "Alias for fs.read";
    set_tool_summary_locale(spec, "en", "Alias for fs.read");
    set_tool_summary_locale(spec, "zh", "fs.read 的别名");
    return spec;
  }

  static ToolExecutionResult run(const ToolExecutionRequest& request){
    if(request.tokens.empty()){
      g_parse_error_cmd = "cat";
      return detail::text_result("usage: cat <path> [options]\n", 1);
    }
    ToolExecutionRequest forwarded = request;
    forwarded.tokens = request.tokens;
    if(!forwarded.tokens.empty()) forwarded.tokens[0] = "fs.read";
    auto result = FsRead::run(forwarded);
    if(result.exitCode != 0){
      g_parse_error_cmd = "cat";
    }
    return result;
  }
};

inline ToolDefinition make_cat_tool(){
  ToolDefinition def;
  def.ui = Cat::ui();
  def.executor = Cat::run;
  return def;
}

} // namespace tool

