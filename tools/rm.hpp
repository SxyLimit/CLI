#pragma once

#include "tool_common.hpp"

namespace tool {

struct Rm {
  static ToolSpec ui(){
    ToolSpec spec;
    spec.name = "rm";
    spec.summary = "Remove files";
    set_tool_summary_locale(spec, "en", "Remove files");
    set_tool_summary_locale(spec, "zh", "删除文件");
    spec.options = {{"-r", false, {}, nullptr, false, "", false}};
    spec.positional = {"<path>"};
    return spec;
  }

  static ToolExecutionResult run(const ToolExecutionRequest& request){
    const auto& args = request.tokens;
    bool recursive = false;
    std::vector<std::string> targets;
    for(size_t i = 1; i < args.size(); ++i){
      if(args[i] == "-r") recursive = true;
      else targets.push_back(args[i]);
    }
    if(targets.empty()){
      g_parse_error_cmd = "rm";
      return detail::text_result("usage: rm [-r] <path> [more paths]\n", 1);
    }
    std::ostringstream oss;
    int exitCode = 0;
    for(const auto& path : targets){
      std::error_code ec;
      if(recursive){
        std::filesystem::remove_all(path, ec);
      }else{
        std::filesystem::remove(path, ec);
      }
      if(ec){
        exitCode = 1;
        oss << "rm: " << path << ": " << ec.message() << "\n";
      }
    }
    ToolExecutionResult result;
    result.exitCode = exitCode;
    result.output = oss.str();
    result.display = result.output;
    return result;
  }
};

inline ToolDefinition make_rm_tool(){
  ToolDefinition def;
  def.ui = Rm::ui();
  def.executor = Rm::run;
  return def;
}

} // namespace tool

