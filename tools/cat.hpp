#pragma once

#include "tool_common.hpp"
#include "agent/fs_read.hpp"

namespace tool {

struct Cat {
  static ToolSpec ui(){
    ToolSpec spec = FsRead::ui();
    spec.name = "cat";
    spec.summary = "Alias for fs.read";
    set_tool_summary_locale(spec, "en", "Alias for fs.read");
    set_tool_summary_locale(spec, "zh", "fs.read 的别名");
    set_tool_help_locale(spec, "en", "cat <path> [options]\nAlias for fs.read with the same options.");
    set_tool_help_locale(spec, "zh", "cat <路径> [选项]\nfs.read 的别名，选项与 fs.read 一致。");
    return spec;
  }

  static ToolExecutionResult run(const ToolExecutionRequest& request){
    if(request.tokens.size() < 2){
      g_parse_error_cmd = "cat";
      return detail::text_result("usage: cat <path> [options]\n", 1);
    }
    ToolExecutionRequest forwarded = request;
    forwarded.tokens = request.tokens;
    if(!forwarded.tokens.empty()) forwarded.tokens[0] = "fs.read";
    auto result = FsRead::run(forwarded);
    if(result.exitCode != 0){
      g_parse_error_cmd = "cat";
      auto rewritePrefix = [](std::string& text){
        if(startsWith(text, "usage: fs.read")){
          text = "usage: cat" + text.substr(std::string("usage: fs.read").size());
        }else if(startsWith(text, "fs.read:")){
          text = "cat:" + text.substr(std::string("fs.read:").size());
        }
      };
      rewritePrefix(result.output);
      if(result.display.has_value()){
        std::string text = *result.display;
        rewritePrefix(text);
        result.display = text;
      }
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
