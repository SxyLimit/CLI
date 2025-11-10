#pragma once

#include "tool_common.hpp"

namespace tool {

struct Llm {
  static ToolSpec ui(){
    ToolSpec spec;
    spec.name = "llm";
    spec.summary = "Call the Python LLM helper";
    set_tool_summary_locale(spec, "en", "Call the Python LLM helper");
    set_tool_summary_locale(spec, "zh", "调用 Python LLM 助手");
    set_tool_help_locale(spec, "en", "llm call <message...> | llm recall");
    set_tool_help_locale(spec, "zh", "llm call <消息...> | llm recall");
    spec.subs = {
      SubcommandSpec{"call", {}, {"<message...>"}, {}, nullptr},
      SubcommandSpec{"recall", {}, {}, {}, nullptr}
    };
    return spec;
  }

  static ToolExecutionResult run(const ToolExecutionRequest& request){
    const auto& args = request.tokens;
    if(args.size() < 2){
      g_parse_error_cmd = "llm";
      return detail::text_result("usage: llm <call|recall>\n", 1);
    }
    const std::string sub = args[1];
    if(sub == "call"){
      if(args.size() < 3){
        g_parse_error_cmd = "llm";
        return detail::text_result("usage: llm call <message>\n", 1);
      }
      std::string cmd = "python3 tools/llm.py call";
      for(size_t i = 2; i < args.size(); ++i){
        cmd += " ";
        cmd += shellEscape(args[i]);
      }
      cmd = "MYCLI_LLM_SILENT=1 " + cmd + " > /dev/null 2>&1";
      try{
        std::thread([command = cmd]{ std::system(command.c_str()); }).detach();
      }catch(const std::system_error&){
        std::system(cmd.c_str());
      }
      llm_set_pending(true);
      return detail::text_result("[llm] request dispatched asynchronously. Use `llm recall` to view replies.\n");
    }
    if(sub == "recall"){
      auto result = detail::execute_shell(request, "python3 tools/llm.py recall");
      llm_poll();
      if(result.exitCode != 0){
        g_parse_error_cmd = "llm";
      }else{
        llm_mark_seen();
      }
      return result;
    }
    g_parse_error_cmd = "llm";
    return detail::text_result("usage: llm <call|recall>\n", 1);
  }
};

inline ToolDefinition make_llm_tool(){
  ToolDefinition def;
  def.ui = Llm::ui();
  def.executor = Llm::run;
  return def;
}

} // namespace tool

