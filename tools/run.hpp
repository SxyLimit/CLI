#pragma once

#include "tool_common.hpp"

namespace tool {

struct RunDemo {
  static ToolSpec ui(){
    ToolSpec spec;
    spec.name = "run";
    spec.summary = "Run a demo job with options";
    set_tool_summary_locale(spec, "en", "Run a demo job with options");
    set_tool_summary_locale(spec, "zh", "运行示例任务（带参数）");
    set_tool_help_locale(spec, "en", "Usage: run [--model <name>] [--topk <k>] [--temp <t>] [--input <path>]");
    set_tool_help_locale(spec, "zh", "用法：run [--model <name>] [--topk <k>] [--temp <t>] [--input <path>]");
    spec.options = {
      {"--model", true, {"alpha","bravo","charlie","delta"}, nullptr, false, "<name>", false},
      {"--topk",  true, {"1","3","5","8","10"},             nullptr, false, "<k>",    false},
      {"--temp",  true, {"0.0","0.2","0.5","0.8","1.0"},    nullptr, false, "<t>",    false},
      {"--input", true, {}, nullptr, false, "<path>", true}
    };
    return spec;
  }

  static ToolExecutionResult run(const ToolExecutionRequest& request){
    const auto& args = request.tokens;
    std::string model = "alpha";
    std::string topk = "5";
    std::string temp = "0.2";
    std::string inputPath;
    for(size_t i = 1; i + 1 < args.size(); ++i){
      if(args[i] == "--model") model = args[i + 1];
      else if(args[i] == "--topk") topk = args[i + 1];
      else if(args[i] == "--temp") temp = args[i + 1];
      else if(args[i] == "--input") inputPath = args[i + 1];
      else if(args[i].rfind("--", 0) == 0){
        g_parse_error_cmd = "run";
        return detail::text_result("unknown option: " + args[i] + "\n", 1);
      }
    }
    std::ostringstream oss;
    oss << "(run) model=" << model
        << " topk=" << topk
        << " temp=" << temp;
    if(!inputPath.empty()) oss << " input=" << inputPath;
    oss << "\n";
    return detail::text_result(oss.str());
  }
};

inline ToolDefinition make_run_tool(){
  ToolDefinition def;
  def.ui = RunDemo::ui();
  def.executor = RunDemo::run;
  return def;
}

} // namespace tool

